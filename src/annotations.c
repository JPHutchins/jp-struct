#include <Python.h>
#include <stdbool.h>

#include "annotations.h"
#include "owned.h"

/* annotationlib.Format. The numbering is the API. */
enum annotation_format {
	ANNOTATION_FORMAT_VALUE = 1,
	ANNOTATION_FORMAT_VALUE_WITH_FAKE_GLOBALS = 2,
	ANNOTATION_FORMAT_FORWARDREF = 3,
	ANNOTATION_FORMAT_STRING = 4,
};

static PyObject * borrow_annotate(PyObject * namespace);
static PyObject * evaluate(PyObject * annotate);

#if PY_VERSION_HEX >= 0x030E0000
/* What a NameError's `.name` says about why it was raised. */
enum symbol_verdict {
	SYMBOL_UNRESOLVED,
	SYMBOL_NOT_A_SYMBOL,
	SYMBOL_UNREADABLE,
};

/* Whether an exception will take a __context__. Not "is it chained": an
 * exception can refuse one while carrying neither a cause nor a context. */
enum context_slot {
	CONTEXT_SLOT_FREE,
	CONTEXT_SLOT_SPOKEN_FOR,
	CONTEXT_SLOT_UNREADABLE,
};

static enum symbol_verdict names_an_unresolved_symbol(PyObject * error);
static PyObject * escalate(PyObject * annotate);
static void raise_over(PyObject * displaced, PyObject * failure);
static enum context_slot context_slot_of(PyObject * error);
#endif

PyObject * struct_annotations(PyObject * const namespace) {
	PyObject * const declared = PyDict_GetItemString(namespace, "__annotations__");

	if (declared != NULL) {
		return Py_NewRef(declared);
	}

	/* Owned rather than borrowed: on 3.14+ evaluate imports a module, which runs
	 * arbitrary Python the first time, and the namespace this came out of is
	 * within that code's reach. */
	PY_OWNED(annotate, Py_XNewRef(borrow_annotate(namespace)));

	if (annotate == NULL) {
		return PyDict_New();
	}

	return evaluate(annotate);
}

/* Borrowed, or NULL when the class body declared no annotations at all. */
static PyObject * borrow_annotate(PyObject * const namespace) {
	PyObject * const annotate = PyDict_GetItemString(namespace, "__annotate__");

	return annotate != NULL ? annotate : PyDict_GetItemString(namespace, "__annotate_func__");
}

/*
 * VALUE first, because it is what the generated __annotate__ implements
 * directly and what all-resolvable annotations -- nearly all of them -- want.
 * Only a name that does not resolve costs anything more.
 *
 * The rescue costs a re-evaluation, and sometimes two: annotationlib rebuilds
 * the callable and re-runs every annotation in the dict, so a class holding one
 * forward reference evaluates its other annotations again. Plain 3.14 evaluates
 * once and defers. Format.STRING would avoid it, because only the keys are read
 * here -- but not for long: #50 adds the #14 ClassVar check, which reads the
 * values, and STRING hands it strings. Not in this tree yet, so the reason to
 * keep FORWARDREF is a pending one rather than a present one.
 *
 * FORWARDREF is the fallback: it leaves an unresolved bare forward reference as
 * a ForwardRef instead of raising, which is all we need since only the field
 * *names* and *order* are read here. It has to come from annotationlib, since a
 * generated __annotate__ evaluates for VALUE and VALUE_WITH_FAKE_GLOBALS -- one
 * branch, and which globals it sees is the caller's business -- and raises
 * NotImplementedError for FORWARDREF and STRING. Measured, having been written
 * from memory three times and been wrong three times. That is what the rebuild
 * is for: annotationlib gives the copy fake globals and calls it with format 2,
 * so the evaluating branch runs against stringifiers.
 *
 * None of this exists below 3.14. annotationlib is stdlib only from there, and
 * so is PEP 649, so an __annotate__ in the namespace is one the class body
 * wrote and VALUE is the whole story.
 */
static PyObject * evaluate(PyObject * const annotate) {
	PY_OWNED(format, PyLong_FromLong(ANNOTATION_FORMAT_VALUE));
	PY_MOVABLE(resolved, format != NULL ? PyObject_CallOneArg(annotate, format) : NULL);

#if PY_VERSION_HEX >= 0x030E0000
	/* Only a plain function: annotationlib's FORWARDREF path rebuilds the
	 * callable from its __globals__ and __code__, so anything else -- a partial,
	 * a callable object -- fails there with an AttributeError that hides the
	 * NameError actually worth reporting. Left unescalated it raises the same
	 * thing a plain function does, and the same thing every version below 3.14
	 * does. */
	if (resolved == NULL && PyFunction_Check(annotate) && PyErr_ExceptionMatches(PyExc_NameError)) {
		PY_MOVABLE(unresolved, PyErr_GetRaisedException());

		switch (names_an_unresolved_symbol(unresolved)) {
			case SYMBOL_UNRESOLVED: {
				PY_MOVABLE(escalated, escalate(annotate));

				if (escalated != NULL) {
					return py_move(&escalated);
				}

				/* Either half of the rescue can fail, and both bury the same
				 * thing. A second bad annotation makes the re-evaluation raise
				 * for its own reasons, and that error says nothing about the
				 * name this started with. */
				raise_over(py_move(&unresolved), PyErr_GetRaisedException());

				return NULL;
			}

			case SYMBOL_UNREADABLE:
				raise_over(py_move(&unresolved), PyErr_GetRaisedException());

				return NULL;

			case SYMBOL_NOT_A_SYMBOL:
				PyErr_SetRaisedException(py_move(&unresolved));

				return NULL;
		}
	}
#endif

	return py_move(&resolved);
}

#if PY_VERSION_HEX >= 0x030E0000
/* The import is a plain path-based one and could in principle find a user
 * module of that name; a checkout that shadows a stdlib module has larger
 * problems.
 *
 * No `owner`: the class does not exist yet, so annotationlib builds every
 * ForwardRef unowned and class-scope resolution is off the table. Only the keys
 * are read here, so nothing depends on it. */
static PyObject * escalate(PyObject * const annotate) {
	PY_OWNED(annotationlib, PyImport_ImportModule("annotationlib"));

	return (
		annotationlib == NULL ? NULL :
		PyObject_CallMethod(
			annotationlib,
			"call_annotate_function",
			"Oi",
			annotate,
			ANNOTATION_FORMAT_FORWARDREF
		)
	);
}

/*
 * One rule for every way the rescue can fail, because writing it out at each
 * failure in turn is what leaked a reference twice.
 *
 * An exit is not a diagnostic: a failure that is not an `Exception` wins over
 * the name it displaced, and one that is loses to it. That is broader than the
 * KeyboardInterrupt and SystemExit it is for -- a GeneratorExit or a
 * BaseException subclass of the annotation's own wins too -- and it is the line
 * Python already draws for what `except Exception` may swallow.
 *
 * The loser goes behind the winner as __context__ only where the winner arrived
 * with no chain of its own, __cause__ counting as much as __context__: a chain
 * the class already had says more than either of these does. Where there is
 * one, the loser is dropped.
 *
 * Both arguments are owned, and neither is NULL: every caller takes its
 * `failure` from PyErr_GetRaisedException straight after a call that returned
 * NULL, and CPython sets an exception on every one of those.
 */
static void raise_over(PyObject * const displaced, PyObject * const failure) {
	bool const exits = !PyErr_GivenExceptionMatches(failure, PyExc_Exception);
	PY_MOVABLE(primary, exits ? failure : displaced);
	PY_MOVABLE(behind, exits ? displaced : failure);

	switch (context_slot_of(primary)) {
		case CONTEXT_SLOT_FREE:
			PyException_SetContext(primary, py_move(&behind));

			break;

		case CONTEXT_SLOT_SPOKEN_FOR:
			break;

		case CONTEXT_SLOT_UNREADABLE: {
			/* Looking raised, on a class the annotation author wrote. An exit
			 * from there is still an exit and takes the same rule the top of
			 * this function applies: it wins, and what it displaced goes behind
			 * it where there is room. That much the two author-controlled reads
			 * share.
			 *
			 * An ordinary failure does not, and the difference is the question
			 * each read was asking. The `.name` read asks what to raise, so its
			 * failure is a candidate answer and the caller puts it behind the
			 * NameError. This one asks whether the winner will accept a
			 * __context__ -- and a read that raised has not said yes, so
			 * nothing is attached, including itself. Attaching it would be
			 * writing to the slot the failed read was asking permission for.
			 *
			 * A second unreadable answer attaches nothing rather than asking a
			 * third time. */
			PY_MOVABLE(looking, PyErr_GetRaisedException());

			if (looking == NULL || PyErr_GivenExceptionMatches(looking, PyExc_Exception)) {
				break;
			}

			if (context_slot_of(looking) == CONTEXT_SLOT_FREE) {
				PyException_SetContext(looking, py_move(&primary));
			}

			PyErr_SetRaisedException(py_move(&looking));

			return;
		}
	}

	PyErr_SetRaisedException(py_move(&primary));
}

/*
 * Whether the exception will take a __context__, which is not the same question
 * as whether it has one -- `raise ... from None` has neither a cause nor a
 * context and still refuses one, which is why this is not called "chained".
 *
 * Both getters hand back a reference, so both are held rather than tested in
 * place, and between them they still miss that case: `from None` stores None as
 * the cause, and PyException_GetCause reports that as no cause at all. The
 * suppression flag is the only thing separating it from an exception that was
 * simply never chained, and there is no C accessor for it.
 *
 * So it is read through the attribute protocol, on an exception whose class the
 * annotation author may have written, and a failure to look is a third answer
 * rather than a guess -- the caller decides, the same way it does for the
 * `.name` lookup. PyObject_IsTrue rather than a test against True, because the
 * interpreter's own traceback printer asks the flag that question and a
 * shadowed truthy value should mean here what it means there.
 */
static enum context_slot context_slot_of(PyObject * const error) {
	PY_OWNED(context, PyException_GetContext(error));
	PY_OWNED(cause, PyException_GetCause(error));

	if (context != NULL || cause != NULL) {
		return CONTEXT_SLOT_SPOKEN_FOR;
	}

	PyObject * found = NULL;

	if (PyObject_GetOptionalAttrString(error, "__suppress_context__", &found) < 0) {
		return CONTEXT_SLOT_UNREADABLE;
	}

	PY_OWNED(suppressed, found);

	if (suppressed == NULL) {
		return CONTEXT_SLOT_FREE;
	}

	int const refused = PyObject_IsTrue(suppressed);

	if (refused < 0) {
		return CONTEXT_SLOT_UNREADABLE;
	}

	return refused == 1 ? CONTEXT_SLOT_SPOKEN_FOR : CONTEXT_SLOT_FREE;
}

/*
 * Not resolving a name is the exemption; arbitrary failure is not, and a
 * NameError the annotation raised for its own reasons is arbitrary failure
 * wearing the right coat. The interpreter fills `name` in when a lookup is what
 * failed, and `raise NameError("...")` leaves it None -- as does anything that
 * is not a non-empty str, which the interpreter never produces.
 *
 * Which is what this can tell apart, and all of it. `raise NameError(name=...)`
 * sets the attribute and reads as a forward reference; so does anything at all,
 * once an *earlier* annotation forward-references, because the rescue
 * re-evaluates the whole dict and stringifies what it cannot resolve. The
 * exemption is best-effort against accidents, not a wall against a caller who
 * wants past it.
 *
 * The lookup runs the attribute protocol on an exception whose class the
 * annotation author controls, so it is a third answer rather than a false one:
 * a failure to look is not "no name", and the caller decides what to raise.
 */
static enum symbol_verdict names_an_unresolved_symbol(PyObject * const error) {
	PyObject * found = NULL;

	if (PyObject_GetOptionalAttrString(error, "name", &found) < 0) {
		return SYMBOL_UNREADABLE;
	}

	PY_OWNED(name, found);

	/* The interpreter sets a real symbol, so a non-str or an empty one is
	 * something the raising code put there. */
	return (
		name != NULL && PyUnicode_Check(
			name
		) && PyUnicode_GET_LENGTH(name) > 0 ? SYMBOL_UNRESOLVED :
		SYMBOL_NOT_A_SYMBOL
	);
}
#endif

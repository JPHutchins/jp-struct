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
static bool names_an_unresolved_symbol(PyObject * error);
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
 * generated __annotate__ answers NotImplementedError to VALUE_WITH_FAKE_GLOBALS
 * only after annotationlib has rebuilt it -- which is the rescue -- and to
 * FORWARDREF and STRING always,
 * and annotationlib is stdlib only from 3.14 -- below that there is no PEP 649
 * either, so an __annotate__ in the namespace is one the class body wrote and
 * VALUE is the whole story.
 *
 * The import is a plain path-based one and could in principle find a user
 * module of that name; a checkout that shadows a stdlib module has larger
 * problems. Whichever half of the escalation fails, the NameError it displaced
 * is put back as the raised one with that failure behind it as __context__, so
 * the name that did not resolve is never the thing that gets lost.
 */
static PyObject * evaluate(PyObject * const annotate) {
	PY_OWNED(format, PyLong_FromLong(ANNOTATION_FORMAT_VALUE));

	if (format == NULL) {
		return NULL;
	}

	PY_MOVABLE(resolved, PyObject_CallOneArg(annotate, format));

#if PY_VERSION_HEX >= 0x030E0000
	/* Only a plain function: annotationlib's FORWARDREF path rebuilds the
	 * callable from its __globals__ and __code__, so anything else -- a partial,
	 * a callable object -- fails there with an AttributeError that hides the
	 * NameError actually worth reporting. Left unescalated it raises the same
	 * thing a plain function does, and the same thing every version below 3.14
	 * does. */
	if (resolved == NULL && PyFunction_Check(annotate) && PyErr_ExceptionMatches(PyExc_NameError)) {
		PY_MOVABLE(unresolved, PyErr_GetRaisedException());

		if (names_an_unresolved_symbol(unresolved)) {
			/* No `owner`: the class does not exist yet, so annotationlib builds
			 * every ForwardRef unowned and class-scope resolution is off the
			 * table. Only the keys are read here, so nothing depends on it. */
			PY_OWNED(annotationlib, PyImport_ImportModule("annotationlib"));
			PY_MOVABLE(
				escalated,
				annotationlib == NULL ? NULL :
					PyObject_CallMethod(
						annotationlib,
						"call_annotate_function",
						"Oi",
						annotate,
						ANNOTATION_FORMAT_FORWARDREF
					)
			);

			if (escalated != NULL) {
				return py_move(&escalated);
			}

			/* Either half can fail, and both bury the same thing. A second bad
			 * annotation makes the re-evaluation raise for its own reasons, and
			 * that error says nothing about the name this started with. */
			PY_MOVABLE(failure, PyErr_GetRaisedException());

			/* Only where there is nothing to lose. A NameError raised while
			 * another exception was being handled arrives with a chain, and
			 * that chain says more about the class than this failure does. */
			if (PyException_GetContext(unresolved) == NULL) {
				PyException_SetContext(unresolved, py_move(&failure));
			}
		}

		PyErr_SetRaisedException(py_move(&unresolved));

		return NULL;
	}
#endif

	return py_move(&resolved);
}

#if PY_VERSION_HEX >= 0x030E0000
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
 */
static bool names_an_unresolved_symbol(PyObject * const error) {
	PyObject * found = NULL;

	if (PyObject_GetOptionalAttrString(error, "name", &found) < 0) {
		return false;
	}

	PY_OWNED(name, found);

	/* The interpreter sets a real symbol, so a non-str or an empty one is
	 * something the raising code put there. */
	return name != NULL && PyUnicode_Check(name) && PyUnicode_GET_LENGTH(name) > 0;
}
#endif

#include <Python.h>

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
 * FORWARDREF is the fallback: it leaves an unresolved bare forward reference as
 * a ForwardRef instead of raising, which is all we need since only the field
 * *names* and *order* are read here. It has to come from annotationlib, since a
 * generated __annotate__ answers NotImplementedError to every format but VALUE,
 * and annotationlib is stdlib only from 3.14 -- below that there is no PEP 649
 * either, so an __annotate__ in the namespace is one the class body wrote and
 * VALUE is the whole story.
 *
 * The import is a plain path-based one and could in principle find a user
 * module of that name; a checkout that shadows a stdlib module has larger
 * problems, and the failure is loud rather than silent.
 */
static PyObject * evaluate(PyObject * const annotate) {
	PY_MOVABLE(resolved, PyObject_CallFunction(annotate, "i", ANNOTATION_FORMAT_VALUE));

#if PY_VERSION_HEX >= 0x030E0000
	if (resolved == NULL && PyErr_ExceptionMatches(PyExc_NameError)) {
		PyErr_Clear();

		PY_OWNED(annotationlib, PyImport_ImportModule("annotationlib"));

		if (annotationlib == NULL) {
			return NULL;
		}

		return PyObject_CallMethod(
			annotationlib,
			"call_annotate_function",
			"Oi",
			annotate,
			ANNOTATION_FORMAT_FORWARDREF
		);
	}
#endif

	return py_move(&resolved);
}

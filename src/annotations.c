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

	PyObject * const annotate = borrow_annotate(namespace);

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

/* annotationlib.Format.FORWARDREF: never raises NameError on an unresolved
 * bare forward reference, which is all we need since we only read the field
 * *names* and *order* here. Only annotationlib can deliver it -- a generated
 * __annotate__ implements VALUE and answers NotImplementedError to the rest --
 * so the call goes through the helper rather than the function itself. */
static PyObject * evaluate(PyObject * const annotate) {
	PY_OWNED(annotationlib, PyImport_ImportModule("annotationlib"));

	if (annotationlib != NULL) {
		return PyObject_CallMethod(
			annotationlib,
			"call_annotate_function",
			"Oi",
			annotate,
			ANNOTATION_FORMAT_FORWARDREF
		);
	}

	if (!PyErr_ExceptionMatches(PyExc_ImportError)) {
		return NULL;
	}

	/* No annotationlib means no PEP 649, so this __annotate__ is one the class
	 * body wrote by hand and a direct call is the only way to reach it. */
	PyErr_Clear();

	return PyObject_CallFunction(annotate, "i", ANNOTATION_FORMAT_VALUE);
}

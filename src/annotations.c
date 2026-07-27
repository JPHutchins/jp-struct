#include <Python.h>

#include "annotations.h"

/* annotationlib.Format.FORWARDREF: never raises NameError on an unresolved
 * bare forward reference, which is all we need since we only read the field
 * *names* and *order* here. */
enum annotation_format {
	ANNOTATION_FORMAT_VALUE = 1,
	ANNOTATION_FORMAT_FORWARDREF = 2,
};

static PyObject * borrow_annotate(PyObject * namespace);
static PyObject * evaluate(PyObject * annotate, enum annotation_format format);

PyObject * record_annotations(PyObject * const namespace) {
	PyObject * const declared = PyDict_GetItemString(namespace, "__annotations__");

	if (declared != NULL) {
		return Py_NewRef(declared);
	}

	PyObject * const annotate = borrow_annotate(namespace);

	if (annotate == NULL) {
		return PyDict_New();
	}

	PyObject * const forwardref = evaluate(annotate, ANNOTATION_FORMAT_FORWARDREF);

	if (forwardref != NULL) {
		return forwardref;
	}

	/* Fall back to VALUE format if FORWARDREF is unsupported. */
	PyErr_Clear();

	return evaluate(annotate, ANNOTATION_FORMAT_VALUE);
}

/* Borrowed, or NULL when the class body declared no annotations at all. */
static PyObject * borrow_annotate(PyObject * const namespace) {
	PyObject * const annotate = PyDict_GetItemString(namespace, "__annotate__");

	return annotate != NULL
		? annotate
		: PyDict_GetItemString(namespace, "__annotate_func__");
}

static PyObject * evaluate(PyObject * const annotate, enum annotation_format const format) {
	PyObject * const argument = PyLong_FromLong(format);

	if (argument == NULL) {
		return NULL;
	}

	PyObject * const annotations = PyObject_CallOneArg(annotate, argument);
	Py_DECREF(argument);

	return annotations;
}

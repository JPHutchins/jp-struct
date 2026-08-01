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

/* annotationlib.Format.FORWARDREF: never raises NameError on an unresolved
 * bare forward reference, which is all we need since we only read the field
 * *names* and *order* here. Only annotationlib can deliver it -- a generated
 * __annotate__ implements VALUE and answers NotImplementedError to the rest --
 * so the call goes through the helper rather than the function itself.
 *
 * Chosen at compile time rather than probed at runtime. annotationlib is stdlib
 * from 3.14; below that, importing the name would find whatever a user happens
 * to have on sys.path and call a function of the same name on it. An older
 * interpreter has no PEP 649 either, so an __annotate__ in the namespace is one
 * the class body wrote, and VALUE is what calling it means. Where the module is
 * stdlib, a failure to import it is a broken interpreter and says so. */
static PyObject * evaluate(PyObject * const annotate) {
#if PY_VERSION_HEX < 0x030E0000
	return PyObject_CallFunction(annotate, "i", ANNOTATION_FORMAT_VALUE);
#else
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
#endif
}

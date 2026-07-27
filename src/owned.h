#pragma once

#include <Python.h>

/*
 * A strong reference released when the scope ends. `cleanup` is a variable
 * attribute rather than a type attribute, so no typedef can carry it and the
 * macro shape is forced; taking the initializer as an argument is what makes
 * an owned reference impossible to declare uninitialized.
 */
#define PY_OWN(name, initializer) \
	__attribute__((cleanup(py_release))) PyObject * name = (initializer)

static inline void py_release(PyObject * * const reference) {
	Py_XDECREF(*reference);
}

/*
 * Hand the reference to the caller. Omitting this where an owned reference is
 * returned is the one mistake the attribute cannot catch: the scope releases
 * what it gave away and the caller is left holding a dead pointer.
 */
static inline PyObject * py_steal(PyObject * * const reference) {
	PyObject * const stolen = *reference;
	*reference = NULL;

	return stolen;
}

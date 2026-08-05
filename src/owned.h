#pragma once

#include <Python.h>

/*
 * A strong reference released when the scope ends. `cleanup` is a variable
 * attribute rather than a type attribute, so no typedef can carry it and the
 * macro shape is forced; taking the initializer as an argument is what makes
 * an owned reference impossible to declare uninitialized.
 */
#define PY_OWNED(name, initializer) \
	__attribute__((cleanup(py_release))) PyObject * const name = (initializer)

/*
 * The same, for a reference whose ownership leaves the scope. `py_move` clears
 * the name so the release at scope exit finds nothing left to drop, which is
 * why this one cannot be const.
 */
#define PY_MOVABLE(name, initializer) \
	__attribute__((cleanup(py_release))) PyObject * name = (initializer)

static inline void py_release(PyObject * const * const reference) {
	Py_XDECREF(*reference);
}

/*
 * Returning a `PY_MOVABLE` name directly instead of moving it is the one
 * mistake the attribute cannot catch: the scope releases what it gave away and
 * the caller is left holding a dead pointer.
 */
static inline PyObject * py_move(PyObject * * const reference) {
	PyObject * const moved = *reference;
	*reference = NULL;

	return moved;
}

/*
 * A reference to the attribute if it is there, and NULL if it is not. NULL with
 * an exception set is a failure to look, which the caller answers rather than
 * clears -- a probe that cannot see has not learned that the attribute is
 * absent.
 *
 * Absent is any AttributeError, subclasses included, which is what `hasattr`
 * and `getattr(x, name, default)` read too: a `__getattr__` raising one is
 * saying the name is not there, whatever type it says it with.
 */
static inline PyObject * optional_attribute(PyObject * const object, char const * const name) {
	PyObject * const value = PyObject_GetAttrString(object, name);

	if (value == NULL && PyErr_ExceptionMatches(PyExc_AttributeError)) {
		PyErr_Clear();
	}

	return value;
}

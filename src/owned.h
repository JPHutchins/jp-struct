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

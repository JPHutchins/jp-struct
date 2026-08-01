#pragma once

#include <Python.h>

PyObject * Struct_vectorcall(
	PyObject * struct_class,
	PyObject * const * arguments,
	size_t argument_count_and_flags,
	PyObject * keyword_names
);

/* salix.set_field(instance, name, value) -- see construct.c. */
PyObject * Struct_set_field(PyObject * module, PyObject * arguments);

/*
 * A default nothing else holds a reference to: a copy for the four mutable
 * builtins, the object itself for everything else. Class creation takes one so
 * the stored default is not the caller's object, and each construction takes
 * another so the instance's is not the stored one.
 */
PyObject * struct_default_copy(PyObject * declared);

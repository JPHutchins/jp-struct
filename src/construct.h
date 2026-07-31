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

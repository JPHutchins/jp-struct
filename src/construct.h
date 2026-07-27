#pragma once

#include <Python.h>

PyObject * Record_vectorcall(
	PyObject * record_class,
	PyObject * const * arguments,
	size_t argument_count_and_flags,
	PyObject * keyword_names
);

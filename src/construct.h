#pragma once

#include <Python.h>
#include <stdbool.h>

PyObject * Struct_vectorcall(
	PyObject * struct_class,
	PyObject * const * arguments,
	size_t argument_count_and_flags,
	PyObject * keyword_names
);

/* salix.set_field(instance, name, value) -- see construct.c. */
PyObject * Struct_set_field(PyObject * module, PyObject * arguments);

/*
 * Whether the type is one of the exact builtins that spell "container I will
 * mutate" -- the question the refusal asks. It and the copy read one table, so
 * the two cannot disagree: a type copied but not refused would be
 * shallow-copied while non-empty, and a type refused but not copied would have
 * its emptiness checked on the caller's object rather than on a private one.
 * Neither drift is the safe direction; the table is why neither is reachable.
 */
bool struct_copies_default(PyTypeObject const * kind);

/*
 * A default nothing else holds a reference to: a copy for the four, the object
 * itself for everything else. Class creation takes one so the stored default is
 * not the caller's object, and each construction takes another so the
 * instance's is not the stored one.
 */
PyObject * struct_default_copy(PyObject * declared);

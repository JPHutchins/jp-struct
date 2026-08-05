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
 * The four exact builtins that spell "container I will mutate", as the refusal
 * sees them. Copying names them a second time, in struct_default_copy, because
 * each one needs its own constructor and no predicate can supply that.
 *
 * The two lists have to agree and nothing makes them: a type copied but not
 * refused gets shallow-copied while non-empty, and a type refused but not
 * copied has its emptiness checked on the caller's object rather than on a
 * private one. A type added here and not there is shared, which is the
 * direction that degrades safely; the other way round changes a value's type.
 */
static inline bool struct_copies_default(PyTypeObject const * const kind) {
	return (
		kind == &PyList_Type ||
		kind == &PyDict_Type ||
		kind == &PySet_Type ||
		kind == &PyByteArray_Type
	);
}

/*
 * A default nothing else holds a reference to: a copy for the four, the object
 * itself for everything else. Class creation takes one so the stored default is
 * not the caller's object, and each construction takes another so the
 * instance's is not the stored one.
 */
PyObject * struct_default_copy(PyObject * declared);

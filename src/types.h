#pragma once

#include <Python.h>

/* PyMemberDef only became visible through Python.h in 3.12. */
#if PY_VERSION_HEX < 0x030C0000
#	include <structmember.h>
#endif

/* An instance of StructMeta *is* a struct class.  We extend the heap-type
 * object with the per-type field metadata needed for fast construction and
 * the dunder methods. */
typedef struct {
	PyHeapTypeObject heap_type;

	/* tuple[str]: every field name, in order */
	PyObject * struct_field_names;

	/* tuple: defaults for the trailing fields */
	PyObject * struct_defaults;

	/* malloc'd array[field_count] of slot offsets */
	Py_ssize_t * struct_slot_offsets;

	Py_ssize_t struct_field_count;
	Py_ssize_t struct_default_count;
} StructType;

static inline StructType * struct_type_of(PyObject * const self) {
	return (StructType *) Py_TYPE(self);
}

static inline char const * struct_type_name(StructType const * const type) {
	return type->heap_type.ht_type.tp_name;
}

/*
 * Where field `index` lives inside an instance. Fields are plain slots, so a
 * struct is just its values laid end to end, and every read or write of one
 * goes through here.
 */
static inline PyObject * * struct_slot(
	StructType const * const type,
	PyObject * const self,
	Py_ssize_t const index
) {
	return (PyObject * *) ((char *) self + type->struct_slot_offsets[index]);
}

/*
 * A slot stays NULL until something writes it, which is observable on a
 * half-built struct. Reading it as None keeps hash and == total instead of
 * making each one re-derive the guard; repr wants to see the NULL, so it
 * reads the slot directly.
 */
static inline PyObject * struct_slot_or_none(
	StructType const * const type,
	PyObject * const self,
	Py_ssize_t const index
) {
	PyObject * const value = *struct_slot(type, self, index);

	return value != NULL ? value : Py_None;
}

/* Fields below this index have no default and must be supplied by the caller. */
static inline Py_ssize_t struct_required_count(StructType const * const type) {
	return type->struct_field_count - type->struct_default_count;
}

/* Both tuples are NULL on the mixin itself, which has no fields; an empty
 * tuple is the honest answer rather than None. */
static inline PyObject * struct_tuple_or_empty(PyObject * const tuple) {
	return tuple != NULL ? Py_NewRef(tuple) : PyTuple_New(0);
}

/* Access the PyMemberDef array that floats behind a heap type. Mirrors
 * msgspec's MS_PyHeapType_GET_MEMBERS: the members live just past the type
 * object, which (for a custom metaclass) is sized by the metaclass basicsize. */
static inline PyMemberDef * struct_heap_type_members(StructType * const type) {
	return (PyMemberDef *) ((char *) type + Py_TYPE(type)->tp_basicsize);
}

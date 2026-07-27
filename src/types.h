#pragma once

#include <Python.h>

/* PyMemberDef only became visible through Python.h in 3.12. */
#if PY_VERSION_HEX < 0x030C0000
#	include <structmember.h>
#endif

/* An instance of RecordMeta *is* a record class.  We extend the heap-type
 * object with the per-type field metadata needed for fast construction and
 * the dunder methods. */
typedef struct {
	PyHeapTypeObject heap_type;

	/* tuple[str]: every field name, in order */
	PyObject * record_field_names;

	/* tuple: defaults for the trailing fields */
	PyObject * record_defaults;

	/* malloc'd array[field_count] of slot offsets */
	Py_ssize_t * record_slot_offsets;

	Py_ssize_t record_field_count;
	Py_ssize_t record_default_count;
} RecordType;

static inline RecordType * record_type_of(PyObject * const self) {
	return (RecordType *) Py_TYPE(self);
}

static inline char const * record_type_name(RecordType const * const type) {
	return type->heap_type.ht_type.tp_name;
}

/*
 * Where field `index` lives inside an instance. Fields are plain slots, so a
 * record is just its values laid end to end, and every read or write of one
 * goes through here.
 */
static inline PyObject * * record_slot(
	RecordType const * const type,
	PyObject * const self,
	Py_ssize_t const index
) {
	return (PyObject * *) ((char *) self + type->record_slot_offsets[index]);
}

/*
 * A slot stays NULL until something writes it, which is observable on a
 * half-built record. Reading it as None keeps hash and == total instead of
 * making each one re-derive the guard; repr wants to see the NULL, so it
 * reads the slot directly.
 */
static inline PyObject * record_slot_or_none(
	RecordType const * const type,
	PyObject * const self,
	Py_ssize_t const index
) {
	PyObject * const value = *record_slot(type, self, index);

	return value != NULL ? value : Py_None;
}

/* Fields below this index have no default and must be supplied by the caller. */
static inline Py_ssize_t record_required_count(RecordType const * const type) {
	return type->record_field_count - type->record_default_count;
}

/* Access the PyMemberDef array that floats behind a heap type. Mirrors
 * msgspec's MS_PyHeapType_GET_MEMBERS: the members live just past the type
 * object, which (for a custom metaclass) is sized by the metaclass basicsize. */
static inline PyMemberDef * record_heap_type_members(RecordType * const type) {
	return (PyMemberDef *) ((char *) type + Py_TYPE(type)->tp_basicsize);
}

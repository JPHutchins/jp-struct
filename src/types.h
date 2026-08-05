#pragma once

#include <Python.h>
#include <stdbool.h>

#include "meta.h"
#include "options.h"

/* PyMemberDef only became visible through Python.h in 3.12, and the member
 * type constants gained their Py_ prefix in the same move. Both halves of that
 * rename live here, so the whole adaptation goes away together whenever the
 * floor reaches 3.12. SLOT_MEMBER_TYPE is what type.__new__ makes a __slots__
 * entry, and so what addresses one by offset. */
#if PY_VERSION_HEX < 0x030C0000
#	include <structmember.h>

enum { SLOT_MEMBER_TYPE = T_OBJECT_EX };
#else
enum { SLOT_MEMBER_TYPE = Py_T_OBJECT_EX };
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

	/* Resolved __post_init__, or NULL for a class that declares none */
	PyObject * struct_post_init;

	Py_ssize_t struct_field_count;
	Py_ssize_t struct_default_count;

	/* What the class body asked for, and what a subclass inherits */
	struct options struct_options;

	/* Whether this class resolves an __eq__ that came from a class body rather
	 * than from salix. Answered once, here, because a subclass needs it and
	 * cannot re-derive it: `__hash__ is None` on the base does not say which
	 * rule put it there. */
	bool struct_resolves_body_eq;
} StructType;

/*
 * Only an instance of StructMeta has the storage declared above; every other
 * type stops at PyHeapTypeObject, and reading a field off one is a read past
 * the end of its allocation. _StructMixin is a permitted base and Struct.__mro__
 * hands it out, so a subclass of it whose metaclass is plain `type` reaches
 * every slot the mixin installs while being no such thing.
 */
static inline bool is_struct_class(PyObject * const object) {
	return PyObject_TypeCheck(object, &StructMeta_Type);
}

static inline bool is_struct(PyObject * const self) {
	return is_struct_class((PyObject *) Py_TYPE(self));
}

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

/* Which of the two metadata tuples is being asked for. The class answers
 * through the metaclass and the instance through the mixin, so without this the
 * same read is written out four times. */
enum struct_metadata : int {
	STRUCT_FIELD_NAMES,
	STRUCT_DEFAULTS,
};

static inline PyObject * struct_metadata(
	StructType const * const type,
	enum struct_metadata const which
) {
	return struct_tuple_or_empty(
		which == STRUCT_DEFAULTS ? type->struct_defaults : type->struct_field_names
	);
}

/* Access the PyMemberDef array that floats behind a heap type. Mirrors
 * msgspec's MS_PyHeapType_GET_MEMBERS: the members live just past the type
 * object, which (for a custom metaclass) is sized by the metaclass basicsize. */
static inline PyMemberDef * struct_heap_type_members(StructType * const type) {
	return (PyMemberDef *) ((char *) type + Py_TYPE(type)->tp_basicsize);
}

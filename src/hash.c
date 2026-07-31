#include <Python.h>

#include "hash.h"
#include "owned.h"
#include "types.h"

/* CPython reserves -1: a real hash of -1 is remapped to -2. */
enum : Py_hash_t {
	HASH_ERR = -1,
};

/*
 * A struct hashes as the tuple of its values, so `hash(p) == hash((1.0, 2.0))`
 * and structs interoperate with tuple keys. Building the tuple is the whole
 * cost; PyObject_Hash does the rest.
 *
 * A struct that holds itself re-enters here through tuplehash without pushing
 * a Python frame, so the interpreter's own depth limit never sees the descent.
 */
Py_hash_t Struct_hash(PyObject * const self) {
	StructType const * const type = struct_type_of(self);
	PY_OWNED(values, PyTuple_New(type->struct_field_count));

	if (values == NULL) {
		return HASH_ERR;
	}

	for (Py_ssize_t i = 0; i < type->struct_field_count; ++i) {
		PyTuple_SET_ITEM(values, i, Py_NewRef(struct_slot_or_none(type, self, i)));
	}

	if (Py_EnterRecursiveCall(" while hashing a struct") != 0) {
		return HASH_ERR;
	}

	Py_hash_t const hash = PyObject_Hash(values);

	Py_LeaveRecursiveCall();

	return hash;
}

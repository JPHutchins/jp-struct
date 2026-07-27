#include <Python.h>

#include "hash.h"
#include "types.h"

/* CPython reserves -1: a real hash of -1 is remapped to -2. */
enum : Py_hash_t {
	HASH_ERR = -1,
};

/*
 * A record hashes as the tuple of its values, so `hash(p) == hash((1.0, 2.0))`
 * and records interoperate with tuple keys. Building the tuple is the whole
 * cost; PyObject_Hash does the rest.
 */
Py_hash_t Record_hash(PyObject * const self) {
	RecordType const * const type = record_type_of(self);
	PyObject * const values = PyTuple_New(type->record_field_count);

	if (values == NULL) {
		return HASH_ERR;
	}

	for (Py_ssize_t i = 0; i < type->record_field_count; ++i) {
		PyTuple_SET_ITEM(values, i, Py_NewRef(record_slot_or_none(type, self, i)));
	}

	Py_hash_t const hash = PyObject_Hash(values);
	Py_DECREF(values);

	return hash;
}

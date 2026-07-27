#include <Python.h>

#include "compare.h"
#include "mixin.h"
#include "types.h"

/* The tri-state PyObject_RichCompareBool speaks, named. */
enum comparison {
	COMPARISON_ERROR = -1,
	COMPARISON_UNEQUAL = 0,
	COMPARISON_EQUAL = 1,
};

static enum comparison records_equal(PyObject * self, PyObject * other);
static enum comparison values_equal(
	RecordType const * self_type,
	PyObject * self,
	RecordType const * other_type,
	PyObject * other
);

PyObject * Record_rich_compare(PyObject * const self, PyObject * const other, int const op) {
	if ((op != Py_EQ && op != Py_NE) || !PyObject_TypeCheck(other, &RecordMixin_Type)) {
		Py_RETURN_NOTIMPLEMENTED;
	}

	switch (records_equal(self, other)) {
		case COMPARISON_ERROR:
			return NULL;
		case COMPARISON_UNEQUAL:
			return PyBool_FromLong(op == Py_NE);
		case COMPARISON_EQUAL:
			return PyBool_FromLong(op == Py_EQ);
	}

	Py_UNREACHABLE();
}

/* Structural: equal iff the field-name tuples match and every value
 * compares equal.  Nominal type identity is deliberately not required. */
static enum comparison records_equal(PyObject * const self, PyObject * const other) {
	RecordType const * const self_type = record_type_of(self);
	RecordType const * const other_type = record_type_of(other);

	int const names_equal = PyObject_RichCompareBool(
		self_type->record_field_names, other_type->record_field_names, Py_EQ
	);

	return names_equal != COMPARISON_EQUAL
		? names_equal
		: values_equal(self_type, self, other_type, other);
}

static enum comparison values_equal(
	RecordType const * const self_type,
	PyObject * const self,
	RecordType const * const other_type,
	PyObject * const other
) {
	for (Py_ssize_t i = 0; i < self_type->record_field_count; ++i) {
		int const equal = PyObject_RichCompareBool(
			record_slot_or_none(self_type, self, i),
			record_slot_or_none(other_type, other, i),
			Py_EQ
		);

		if (equal != COMPARISON_EQUAL) {
			return equal;
		}
	}

	return COMPARISON_EQUAL;
}

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

static enum comparison structs_equal(PyObject * self, PyObject * other);
static enum comparison values_equal(
	StructType const * self_type,
	PyObject * self,
	StructType const * other_type,
	PyObject * other
);

PyObject * Struct_rich_compare(PyObject * const self, PyObject * const other, int const op) {
	if ((op != Py_EQ && op != Py_NE) || !PyObject_TypeCheck(other, &StructMixin_Type)) {
		Py_RETURN_NOTIMPLEMENTED;
	}

	switch (structs_equal(self, other)) {
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
static enum comparison structs_equal(PyObject * const self, PyObject * const other) {
	StructType const * const self_type = struct_type_of(self);
	StructType const * const other_type = struct_type_of(other);

	int const names_equal = PyObject_RichCompareBool(
		self_type->struct_field_names, other_type->struct_field_names, Py_EQ
	);

	return names_equal != COMPARISON_EQUAL
		? names_equal
		: values_equal(self_type, self, other_type, other);
}

static enum comparison values_equal(
	StructType const * const self_type,
	PyObject * const self,
	StructType const * const other_type,
	PyObject * const other
) {
	for (Py_ssize_t i = 0; i < self_type->struct_field_count; ++i) {
		int const equal = PyObject_RichCompareBool(
			struct_slot_or_none(self_type, self, i),
			struct_slot_or_none(other_type, other, i),
			Py_EQ
		);

		if (equal != COMPARISON_EQUAL) {
			return equal;
		}
	}

	return COMPARISON_EQUAL;
}

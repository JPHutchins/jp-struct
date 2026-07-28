#include <Python.h>

#include "owned.h"
#include "repr.h"
#include "types.h"

static PyObject * fields_repr(StructType const * type, PyObject * self);
static PyObject * field_repr(StructType const * type, PyObject * self, Py_ssize_t index);

/*
 * Py_ReprEnter/Py_ReprLeave bracket exactly one call each here, so a struct
 * that contains itself renders as `...` instead of recursing forever.
 */
PyObject * Struct_repr(PyObject * const self) {
	int const recursive = Py_ReprEnter(self);

	if (recursive != 0) {
		return recursive < 0 ? NULL : PyUnicode_FromString("...");
	}

	PY_OWNED(inner, fields_repr(struct_type_of(self), self));
	Py_ReprLeave(self);

	if (inner == NULL) {
		return NULL;
	}

	return PyUnicode_FromFormat("%s(%U)", Py_TYPE(self)->tp_name, inner);
}

/* The `x=1.0, y=2.0` interior, without the class name or the parentheses. */
static PyObject * fields_repr(StructType const * const type, PyObject * const self) {
	PY_OWNED(pieces, PyList_New(type->struct_field_count));

	if (pieces == NULL) {
		return NULL;
	}

	for (Py_ssize_t i = 0; i < type->struct_field_count; ++i) {
		PyObject * const piece = field_repr(type, self, i);

		if (piece == NULL) {
			return NULL;
		}

		PyList_SET_ITEM(pieces, i, piece);
	}

	PY_OWNED(separator, PyUnicode_FromString(", "));

	return separator != NULL ? PyUnicode_Join(separator, pieces) : NULL;
}

/* `name=value`, or `name=<unset>` for a slot nothing has written yet. */
static PyObject * field_repr(
	StructType const * const type,
	PyObject * const self,
	Py_ssize_t const index
) {
	PyObject * const value = *struct_slot(type, self, index);

	PY_OWNED(rendered, value != NULL ? PyObject_Repr(value) : PyUnicode_FromString("<unset>"));

	if (rendered == NULL) {
		return NULL;
	}

	return PyUnicode_FromFormat(
		"%U=%U", PyTuple_GET_ITEM(type->struct_field_names, index), rendered
	);
}

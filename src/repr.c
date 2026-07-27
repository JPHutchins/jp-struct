#include <Python.h>

#include "repr.h"
#include "types.h"

static PyObject * fields_repr(RecordType const * type, PyObject * self);
static PyObject * field_repr(RecordType const * type, PyObject * self, Py_ssize_t index);

/*
 * Py_ReprEnter/Py_ReprLeave bracket exactly one call each here, so a record
 * that contains itself renders as `...` instead of recursing forever.
 */
PyObject * Record_repr(PyObject * const self) {
	int const recursive = Py_ReprEnter(self);

	if (recursive != 0) {
		return recursive < 0 ? NULL : PyUnicode_FromString("...");
	}

	PyObject * const inner = fields_repr(record_type_of(self), self);
	Py_ReprLeave(self);

	if (inner == NULL) {
		return NULL;
	}

	PyObject * const out = PyUnicode_FromFormat("%s(%U)", Py_TYPE(self)->tp_name, inner);
	Py_DECREF(inner);

	return out;
}

/* The `x=1.0, y=2.0` interior, without the class name or the parentheses. */
static PyObject * fields_repr(RecordType const * const type, PyObject * const self) {
	PyObject * const pieces = PyList_New(type->record_field_count);

	if (pieces == NULL) {
		return NULL;
	}

	for (Py_ssize_t i = 0; i < type->record_field_count; ++i) {
		PyObject * const piece = field_repr(type, self, i);

		if (piece == NULL) {
			Py_DECREF(pieces);

			return NULL;
		}

		PyList_SET_ITEM(pieces, i, piece);
	}

	PyObject * const separator = PyUnicode_FromString(", ");
	PyObject * const joined = separator != NULL ? PyUnicode_Join(separator, pieces) : NULL;

	Py_XDECREF(separator);
	Py_DECREF(pieces);

	return joined;
}

/* `name=value`, or `name=<unset>` for a slot nothing has written yet. */
static PyObject * field_repr(
	RecordType const * const type,
	PyObject * const self,
	Py_ssize_t const index
) {
	PyObject * const value = *record_slot(type, self, index);
	PyObject * const rendered = value != NULL
		? PyObject_Repr(value)
		: PyUnicode_FromString("<unset>");

	if (rendered == NULL) {
		return NULL;
	}

	PyObject * const piece = PyUnicode_FromFormat(
		"%U=%U", PyTuple_GET_ITEM(type->record_field_names, index), rendered
	);
	Py_DECREF(rendered);

	return piece;
}

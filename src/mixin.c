#include <Python.h>

#include "compare.h"
#include "hash.h"
#include "mixin.h"
#include "repr.h"
#include "result.h"
#include "types.h"

static int Record_set_attribute(PyObject * self, PyObject * name, PyObject * value);
static PyObject * Record_get_field_names(PyObject * self, void * closure);
static PyObject * Record_get_defaults(PyObject * self, void * closure);
static PyObject * tuple_or_empty(PyObject * tuple);
static PyGetSetDef Record_getset[];

PyTypeObject RecordMixin_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) .tp_name = "record._RecordMixin",
	.tp_basicsize = sizeof(PyObject),
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_setattro = Record_set_attribute,
	.tp_repr = Record_repr,
	.tp_hash = Record_hash,
	.tp_richcompare = Record_rich_compare,
	.tp_getset = Record_getset,
};

static PyGetSetDef Record_getset[] = {
	{
		.name = "__record_fields__",
		.get = Record_get_field_names,
		.doc = "tuple of field names",
	},
	{
		.name = "__record_defaults__",
		.get = Record_get_defaults,
		.doc = "tuple of trailing defaults",
	},
	{.name = NULL},
};

/* Records are frozen, so every write fails.  A NULL value is how CPython
 * spells `del`, which is the only thing the two messages differ over. */
static int Record_set_attribute(
	PyObject * const self,
	PyObject * const name,
	PyObject * const value
) {
	PyErr_Format(
		PyExc_TypeError,
		"%.200s object does not support attribute %s",
		Py_TYPE(self)->tp_name,
		value == NULL ? "deletion" : "assignment"
	);

	return RESULT_ERROR;
}

static PyObject * Record_get_field_names(PyObject * const self, void * const closure) {
	return tuple_or_empty(record_type_of(self)->record_field_names);
}

static PyObject * Record_get_defaults(PyObject * const self, void * const closure) {
	return tuple_or_empty(record_type_of(self)->record_defaults);
}

/* Both tuples are NULL on the mixin itself, which has no fields; an empty
 * tuple is the honest answer rather than None. */
static PyObject * tuple_or_empty(PyObject * const tuple) {
	return tuple != NULL ? Py_NewRef(tuple) : PyTuple_New(0);
}

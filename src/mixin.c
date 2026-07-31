#include <Python.h>

#include "compare.h"
#include "hash.h"
#include "mixin.h"
#include "repr.h"
#include "result.h"
#include "types.h"

static int Struct_set_attribute(PyObject * self, PyObject * name, PyObject * value);
static PyObject * Struct_get_field_names(PyObject * self, void * closure);
static PyObject * Struct_get_defaults(PyObject * self, void * closure);
static PyObject * not_a_struct(PyObject * self, char const * name);
static PyGetSetDef Struct_getset[];

PyTypeObject StructMixin_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "salix._StructMixin",
	.tp_basicsize = sizeof(PyObject),
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	.tp_setattro = Struct_set_attribute,
	.tp_repr = Struct_repr,
	.tp_hash = Struct_hash,
	.tp_richcompare = Struct_rich_compare,
	.tp_getset = Struct_getset,
};

static PyGetSetDef Struct_getset[] = {
	{
		.name = "__struct_fields__",
		.get = Struct_get_field_names,
		.doc = "tuple of field names",
	},
	{
		.name = "__struct_defaults__",
		.get = Struct_get_defaults,
		.doc = "tuple of trailing defaults",
	},
	{.name = NULL},
};

/*
 * Everything below asks whether it is looking at a struct, because the mixin is
 * a permitted base and a subclass of it need not be one. Where there is no
 * struct the mixin has nothing to contribute, so the object gets what it would
 * have had without it -- raising out of repr or hash would turn an object that
 * is merely useless into one a debugger cannot print. The two getsets are the
 * exception: they report struct metadata and nothing else, so there is no
 * behaviour to fall back to.
 */
static PyObject * not_a_struct(PyObject * const self, char const * const name) {
	PyErr_Format(
		PyExc_TypeError,
		"%s is defined on structs, and %.200s is not one",
		name,
		Py_TYPE(self)->tp_name
	);

	return NULL;
}

/* Structs are frozen, so every write fails.  A NULL value is how CPython
 * spells `del`, which is the only thing the two messages differ over. */
static int Struct_set_attribute(
	PyObject * const self,
	PyObject * const name,
	PyObject * const value
) {
	if (!is_struct(self)) {
		return PyObject_GenericSetAttr(self, name, value);
	}

	PyErr_Format(
		PyExc_TypeError,
		"%.200s object does not support attribute %s",
		Py_TYPE(self)->tp_name,
		value == NULL ? "deletion" : "assignment"
	);

	return RESULT_ERROR;
}

static PyObject * Struct_get_field_names(PyObject * const self, void * const closure) {
	return (
		is_struct(self) ? struct_tuple_or_empty(struct_type_of(self)->struct_field_names) :
		not_a_struct(self, "__struct_fields__")
	);
}

static PyObject * Struct_get_defaults(PyObject * const self, void * const closure) {
	return (
		is_struct(self) ? struct_tuple_or_empty(struct_type_of(self)->struct_defaults) :
		not_a_struct(self, "__struct_defaults__")
	);
}

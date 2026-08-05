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
static PyObject * metadata_of(PyObject * self, enum struct_metadata which, char const * name);
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
 * struct, repr falls back to object's rather than raising -- raising out of
 * repr would turn an object that is merely useless into one a debugger cannot
 * print.
 *
 * object's, specifically, and not the co-base's: an impostor over `list` is
 * given object's repr, where a plain list subclass has its own. Deferring to
 * whatever the co-base defines would mean walking the MRO for something to
 * borrow, on a path reachable only by subclassing a private class.
 *
 * Hash is the exception, and #66 is why: it cannot be a local decision, because
 * hashing and equality have to agree and equality here is the co-base's. It
 * refuses instead. See src/hash.c.
 *
 * The two getsets are the exception either way: they report struct metadata and
 * nothing else, so there is nothing to fall back to and they raise. An
 * AttributeError rather than a TypeError, because "this object does not have
 * that attribute" is what happened and it is what `hasattr` and `getattr`'s
 * default are written to catch.
 */
static PyObject * metadata_of(
	PyObject * const self,
	enum struct_metadata const which,
	char const * const name
) {
	if (is_struct(self)) {
		return struct_metadata(struct_type_of(self), which);
	}

	PyErr_Format(
		PyExc_AttributeError,
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

/* Two entry points because the getset table needs two; the answer is one. */
static PyObject * Struct_get_field_names(PyObject * const self, void * const closure) {
	return metadata_of(self, STRUCT_FIELD_NAMES, "__struct_fields__");
}

static PyObject * Struct_get_defaults(PyObject * const self, void * const closure) {
	return metadata_of(self, STRUCT_DEFAULTS, "__struct_defaults__");
}

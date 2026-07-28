#include <Python.h>
#include <stddef.h>

#include "construct.h"
#include "fields.h"
#include "meta.h"
#include "owned.h"
#include "result.h"
#include "types.h"

#ifndef Py_TPFLAGS_HAVE_VECTORCALL
#	define Py_TPFLAGS_HAVE_VECTORCALL _Py_TPFLAGS_HAVE_VECTORCALL
#endif

/* Where type.__new__ placed the slot it created for a field name. */
struct member_lookup {
	enum { MEMBER_LOOKUP_FOUND, MEMBER_LOOKUP_MISSING } tag;
	Py_ssize_t offset;
};

static int StructMeta_traverse(PyObject * self, visitproc visit, void * arg);
static int StructMeta_clear(PyObject * self);
static void StructMeta_dealloc(PyObject * self);

static StructType * find_struct_base(PyObject * bases);
static PyObject * build_class_namespace(
	PyObject * original_namespace,
	PyObject * all_names,
	PyObject * new_names
);
static enum result drop_class_variables(PyObject * namespace, PyObject * new_names);
static StructType * create_class(
	PyTypeObject * metatype,
	PyObject * name,
	PyObject * bases,
	PyObject * namespace
);
static enum result install_fields(
	StructType * struct_class,
	StructType const * base,
	struct field_plan const * plan
);
static Py_ssize_t * resolve_slot_offsets(
	StructType * struct_class,
	StructType const * base,
	PyObject * new_names,
	Py_ssize_t field_count
);
static struct member_lookup find_member(
	PyMemberDef const * members,
	Py_ssize_t member_count,
	PyObject * name
);

PyTypeObject StructMeta_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) .tp_name = "jpstruct.StructMeta",
	.tp_basicsize = sizeof(StructType),
	.tp_itemsize = sizeof(PyMemberDef),
	.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_TYPE_SUBCLASS | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_VECTORCALL | Py_TPFLAGS_BASETYPE,
	.tp_new = StructMeta_new,
	.tp_dealloc = StructMeta_dealloc,
	.tp_traverse = StructMeta_traverse,
	.tp_clear = StructMeta_clear,
	.tp_call = PyVectorcall_Call,
	.tp_vectorcall_offset = offsetof(PyTypeObject, tp_vectorcall),
};

/*
 * Creating a struct class is four steps: work out the fields, build the
 * namespace type.__new__ wants, make the type, then hand it the field table
 * that makes it a struct.
 */
PyObject * StructMeta_new(
	PyTypeObject * const metatype,
	PyObject * const args,
	PyObject * const keywords  /* class keywords (frozen=, kw_only=, ...) not yet supported */
) {
	PyObject * name;
	PyObject * bases;
	PyObject * original_namespace;

	if (
		!PyArg_ParseTuple(
			args,
			"UO!O!:StructMeta.__new__",
			&name,
			&PyTuple_Type,
			&bases,
			&PyDict_Type,
			&original_namespace
		)
	) {
		return NULL;
	}

	StructType const * const base = find_struct_base(bases);
	struct field_plan plan = field_plan_build(base, original_namespace);

	if (field_plan_failed(&plan)) {
		return NULL;
	}

	PY_OWNED(namespace, build_class_namespace(original_namespace, plan.all_names, plan.new_names));
	StructType * struct_class =
		namespace != NULL ? create_class(metatype, name, bases, namespace) : NULL;

	if (struct_class != NULL && install_fields(struct_class, base, &plan) != RESULT_OK) {
		Py_CLEAR(struct_class);
	}

	field_plan_clear(&plan);

	return (PyObject *) struct_class;
}

/* Find the (single) struct base among ``bases``, or NULL if none has fields. */
static StructType * find_struct_base(PyObject * const bases) {
	for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); ++i) {
		PyObject * const base = PyTuple_GET_ITEM(bases, i);

		if (PyObject_TypeCheck(base, &StructMeta_Type)
			&& ((StructType *) base)->struct_field_count > 0) {
			return (StructType *) base;
		}
	}

	return NULL;
}

/* The namespace handed to type.__new__: a copy of the original with the
 * default-bearing field names removed (so __slots__ won't clash with a class
 * variable) plus __slots__ / __match_args__. */
static PyObject * build_class_namespace(
	PyObject * const original_namespace,
	PyObject * const all_names,
	PyObject * const new_names
) {
	PY_OWNED(slots, PyList_AsTuple(new_names));
	PY_OWNED(match_args, PyList_AsTuple(all_names));
	PY_MOVABLE(namespace, PyDict_Copy(original_namespace));

	if (slots != NULL
		&& match_args != NULL
		&& namespace != NULL
		&& drop_class_variables(namespace, new_names) == RESULT_OK
		&& PyDict_SetItemString(namespace, "__slots__", slots) == 0
		&& PyDict_SetItemString(namespace, "__match_args__", match_args) == 0) {
		return py_move(&namespace);
	}

	return NULL;
}

/* A field with a default is bound in the class body, where it would collide
 * with the __slots__ descriptor of the same name. */
static enum result drop_class_variables(PyObject * const namespace, PyObject * const new_names) {
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(new_names); ++i) {
		PyObject * const field_name = PyList_GET_ITEM(new_names, i);

		if (PyDict_Contains(namespace, field_name) == 1
			&& PyDict_DelItem(namespace, field_name) < 0) {
			return RESULT_ERROR;
		}
	}

	return RESULT_OK;
}

static StructType * create_class(
	PyTypeObject * const metatype,
	PyObject * const name,
	PyObject * const bases,
	PyObject * const namespace
) {
	PY_OWNED(type_args, PyTuple_Pack(3, name, bases, namespace));

	if (type_args == NULL) {
		return NULL;
	}

	return (StructType *) PyType_Type.tp_new(metatype, type_args, NULL);
}

/* The type exists but is not yet a struct; this is what makes it one. */
static enum result install_fields(
	StructType * const struct_class,
	StructType const * const base,
	struct field_plan const * const plan
) {
	PY_MOVABLE(field_names, PyList_AsTuple(plan->all_names));

	if (field_names == NULL) {
		return RESULT_ERROR;
	}

	Py_ssize_t const field_count = PyTuple_GET_SIZE(field_names);
	Py_ssize_t * const offsets =
		resolve_slot_offsets(struct_class, base, plan->new_names, field_count);

	if (offsets == NULL) {
		return RESULT_ERROR;
	}

	struct_class->struct_field_names = py_move(&field_names);
	struct_class->struct_defaults = Py_NewRef(plan->defaults);
	struct_class->struct_slot_offsets = offsets;
	struct_class->struct_field_count = field_count;
	struct_class->struct_default_count = PyTuple_GET_SIZE(plan->defaults);
	struct_class->heap_type.ht_type.tp_vectorcall = Struct_vectorcall;

	return RESULT_OK;
}

/* Inherited fields keep the base's offsets; new ones are wherever type.__new__
 * just placed the slots it created from __slots__. */
static Py_ssize_t * resolve_slot_offsets(
	StructType * const struct_class,
	StructType const * const base,
	PyObject * const new_names,
	Py_ssize_t const field_count
) {
	Py_ssize_t * const offsets = PyMem_New(Py_ssize_t, field_count > 0 ? field_count : 1);

	if (offsets == NULL) {
		PyErr_NoMemory();

		return NULL;
	}

	Py_ssize_t const inherited_count = base != NULL ? base->struct_field_count : 0;

	for (Py_ssize_t i = 0; i < inherited_count; ++i) {
		offsets[i] = base->struct_slot_offsets[i];
	}

	PyMemberDef const * const members = struct_heap_type_members(struct_class);
	Py_ssize_t const member_count = Py_SIZE(struct_class);

	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(new_names); ++i) {
		PyObject * const field_name = PyList_GET_ITEM(new_names, i);
		struct member_lookup const found = find_member(members, member_count, field_name);

		switch (found.tag) {
			case MEMBER_LOOKUP_MISSING:
				PyErr_Format(PyExc_RuntimeError, "could not find slot offset for %R", field_name);
				PyMem_Free(offsets);

				return NULL;
			case MEMBER_LOOKUP_FOUND:
				offsets[inherited_count + i] = found.offset;
		}
	}

	return offsets;
}

static struct member_lookup find_member(
	PyMemberDef const * const members,
	Py_ssize_t const member_count,
	PyObject * const name
) {
	for (Py_ssize_t i = 0; i < member_count; ++i) {
		if (PyUnicode_CompareWithASCIIString(name, members[i].name) == 0) {
			return (struct member_lookup) {
				.tag = MEMBER_LOOKUP_FOUND, .offset = members[i].offset
			};
		}
	}

	return (struct member_lookup) { .tag = MEMBER_LOOKUP_MISSING };
}

/* `visit` and `arg` are not free names: Py_VISIT expands to reference both by
 * those exact spellings, so renaming either one stops the macro compiling. */
static int StructMeta_traverse(PyObject * const self, visitproc const visit, void * const arg) {
	StructType * const struct_class = (StructType *) self;

	Py_VISIT(struct_class->struct_field_names);
	Py_VISIT(struct_class->struct_defaults);

	return PyType_Type.tp_traverse(self, visit, arg);
}

static int StructMeta_clear(PyObject * const self) {
	StructType * const struct_class = (StructType *) self;

	if (struct_class->struct_field_names == NULL) {  /* already cleared */
		return RESULT_OK;
	}

	Py_CLEAR(struct_class->struct_field_names);
	Py_CLEAR(struct_class->struct_defaults);
	PyMem_Free(struct_class->struct_slot_offsets);
	struct_class->struct_slot_offsets = NULL;

	return PyType_Type.tp_clear(self);
}

static void StructMeta_dealloc(PyObject * const self) {
	/* GC invariants require dealloc to untrack immediately, but
	 * PyType_Type.tp_dealloc assumes the type is currently tracked — hence the
	 * untrack / clear / re-track dance (mirrors msgspec's StructMeta_dealloc). */
	PyObject_GC_UnTrack(self);
	StructMeta_clear(self);
	PyObject_GC_Track(self);
	PyType_Type.tp_dealloc(self);
}

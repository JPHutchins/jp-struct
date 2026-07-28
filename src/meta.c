#include <Python.h>
#include <stdbool.h>
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

/* Whether a class body asked to be mutable, and whether it was allowed to. */
struct mutability {
	enum { MUTABILITY_RESOLVED, MUTABILITY_REJECTED } tag;
	bool frozen;
};

/* Where type.__new__ placed the slot it created for a field name. */
struct member_lookup {
	enum { MEMBER_LOOKUP_FOUND, MEMBER_LOOKUP_MISSING } tag;
	Py_ssize_t offset;
};

static int StructMeta_traverse(PyObject * self, visitproc visit, void * arg);
static int StructMeta_clear(PyObject * self);
static void StructMeta_dealloc(PyObject * self);

static StructType * find_struct_base(PyObject * bases);
static struct mutability read_mutability(PyObject * keywords, StructType const * base);
static bool inherits_frozen(StructType const * base);
static enum result make_mutable(PyObject * namespace);
static PyObject * build_class_namespace(
	PyObject * original_namespace,
	PyObject * all_names,
	PyObject * new_names,
	bool frozen
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
static PyObject * StructMeta_get_field_names(PyObject * self, void * closure);
static PyObject * StructMeta_get_defaults(PyObject * self, void * closure);
static PyGetSetDef StructMeta_getset[];

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
	.tp_getset = StructMeta_getset,
};

/* The mixin answers these for an instance; the metaclass answers the same
 * questions of the class, which is where msgspec puts them and so where a
 * reader looks first. */
static PyGetSetDef StructMeta_getset[] = {
	{
		.name = "__struct_fields__",
		.get = StructMeta_get_field_names,
		.doc = "tuple of field names",
	},
	{
		.name = "__struct_defaults__",
		.get = StructMeta_get_defaults,
		.doc = "tuple of trailing defaults",
	},
	{.name = NULL},
};

static PyObject * StructMeta_get_field_names(PyObject * const self, void * const closure) {
	return struct_tuple_or_empty(((StructType *) self)->struct_field_names);
}

static PyObject * StructMeta_get_defaults(PyObject * const self, void * const closure) {
	return struct_tuple_or_empty(((StructType *) self)->struct_defaults);
}

/*
 * Creating a struct class is four steps: work out the fields, build the
 * namespace type.__new__ wants, make the type, then hand it the field table
 * that makes it a struct.
 */
PyObject * StructMeta_new(
	PyTypeObject * const metatype,
	PyObject * const args,
	PyObject * const keywords
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
	struct mutability const mutability = read_mutability(keywords, base);

	if (mutability.tag == MUTABILITY_REJECTED) {
		return NULL;
	}

	struct field_plan plan = field_plan_build(base, original_namespace);

	if (field_plan_failed(&plan)) {
		return NULL;
	}

	PY_OWNED(
		namespace,
		build_class_namespace(
			original_namespace, plan.all_names, plan.new_names, mutability.frozen
		)
	);
	StructType * struct_class =
		namespace != NULL ? create_class(metatype, name, bases, namespace) : NULL;

	if (struct_class != NULL && install_fields(struct_class, base, &plan) != RESULT_OK) {
		Py_CLEAR(struct_class);
	}

	field_plan_clear(&plan);

	return (PyObject *) struct_class;
}

/*
 * `frozen` is the only class keyword; anything else is a typo, and silently
 * ignoring it would leave a struct frozen when the body said otherwise.
 *
 * Mutability is inherited, and disagreeing with the base is refused rather than
 * resolved: a subclass that unfreezes its base hands out a mutable object to
 * everything holding a reference of the base's type. A base with no fields
 * imposes nothing -- there is nothing to mutate -- which is what lets a first
 * subclass of Struct ask to be mutable at all.
 */
static struct mutability read_mutability(PyObject * const keywords, StructType const * const base) {
	bool const inherited = inherits_frozen(base);

	if (keywords == NULL || PyDict_GET_SIZE(keywords) == 0) {
		return (struct mutability) { .tag = MUTABILITY_RESOLVED, .frozen = inherited };
	}

	PyObject * const requested = PyDict_GetItemString(keywords, "frozen");

	if (requested == NULL || PyDict_GET_SIZE(keywords) != 1) {
		PyErr_SetString(
			PyExc_TypeError, "the only class keyword a struct takes is 'frozen'"
		);

		return (struct mutability) { .tag = MUTABILITY_REJECTED };
	}

	int const frozen = PyObject_IsTrue(requested);

	if (frozen < 0) {
		return (struct mutability) { .tag = MUTABILITY_REJECTED };
	}

	if (base != NULL && (frozen != 0) != inherited) {
		PyErr_Format(
			PyExc_TypeError,
			"%s struct cannot inherit from a %s one",
			frozen != 0 ? "a frozen" : "a mutable",
			inherited ? "frozen" : "mutable"
		);

		return (struct mutability) { .tag = MUTABILITY_REJECTED };
	}

	return (struct mutability) { .tag = MUTABILITY_RESOLVED, .frozen = frozen != 0 };
}

/* Frozen is the default, and what the mixin's tp_setattro enforces; a mutable
 * class is the one that replaced it with the generic implementation. */
static bool inherits_frozen(StructType const * const base) {
	return base == NULL || base->heap_type.ht_type.tp_setattro != PyObject_GenericSetAttr;
}

/*
 * Bound in the namespace rather than written to the slots afterwards: the type
 * machinery derives tp_setattro and tp_hash from what the class body defines,
 * so assigning the slots directly would leave __setattr__ still resolving to
 * the mixin's while `instance.field = value` took the generic path.
 */
static enum result make_mutable(PyObject * const namespace) {
	PyObject * const object_type = (PyObject *) &PyBaseObject_Type;

	for (char const * const * name = (char const *[]){ "__setattr__", "__delattr__", NULL };
		 *name != NULL;
		 ++name) {
		PY_OWNED(generic, PyObject_GetAttrString(object_type, *name));

		if (generic == NULL || PyDict_SetItemString(namespace, *name, generic) < 0) {
			return RESULT_ERROR;
		}
	}

	/* A value whose hash would move under its own key is not a key. */
	if (PyDict_GetItemString(namespace, "__hash__") == NULL
		&& PyDict_SetItemString(namespace, "__hash__", Py_None) < 0) {
		return RESULT_ERROR;
	}

	return RESULT_OK;
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
	PyObject * const new_names,
	bool const frozen
) {
	PY_OWNED(slots, PyList_AsTuple(new_names));
	PY_OWNED(match_args, PyList_AsTuple(all_names));
	PY_MOVABLE(namespace, PyDict_Copy(original_namespace));

	if (slots != NULL
		&& match_args != NULL
		&& namespace != NULL
		&& drop_class_variables(namespace, new_names) == RESULT_OK
		&& PyDict_SetItemString(namespace, "__slots__", slots) == 0
		&& PyDict_SetItemString(namespace, "__match_args__", match_args) == 0
		&& (frozen || make_mutable(namespace) == RESULT_OK)) {
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

static PyObject * StructMeta_get_field_names(PyObject * self, void * closure);
static PyObject * StructMeta_get_defaults(PyObject * self, void * closure);
static PyGetSetDef StructMeta_getset[];

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

#ifdef TESTING

#	include "testing.h"

/* find_member reads a PyMemberDef array, which is trivially fabricated -- and
 * the miss is the branch that turns into a RuntimeError nothing else exercises. */
static PyMemberDef const example_members[] = {
	{.name = "alpha", .offset = 16},
	{.name = "beta", .offset = 24},
	{.name = NULL},
};

static void test_a_declared_member_yields_its_offset(void) {
	PyObject * const name = PyUnicode_FromString("beta");
	struct member_lookup const found = find_member(example_members, 2, name);

	TEST_ASSERT_EQUAL_INT(MEMBER_LOOKUP_FOUND, found.tag);
	TEST_ASSERT_EQUAL_INT(24, found.offset);

	Py_DECREF(name);
}

static void test_an_undeclared_member_is_missing(void) {
	PyObject * const name = PyUnicode_FromString("gamma");
	struct member_lookup const found = find_member(example_members, 2, name);

	TEST_ASSERT_EQUAL_INT(MEMBER_LOOKUP_MISSING, found.tag);

	Py_DECREF(name);
}

static void test_the_search_respects_the_declared_count(void) {
	PyObject * const name = PyUnicode_FromString("beta");
	struct member_lookup const found = find_member(example_members, 1, name);

	TEST_ASSERT_EQUAL_INT(MEMBER_LOOKUP_MISSING, found.tag);

	Py_DECREF(name);
}

void meta_tests(void) {
	/* Unity takes its file from UNITY_BEGIN, which is the runner's. */
	Unity.TestFile = __FILE__;

	RUN_TEST(test_a_declared_member_yields_its_offset);
	RUN_TEST(test_an_undeclared_member_is_missing);
	RUN_TEST(test_the_search_respects_the_declared_count);
}

#endif

static void StructMeta_dealloc(PyObject * const self) {
	/* GC invariants require dealloc to untrack immediately, but
	 * PyType_Type.tp_dealloc assumes the type is currently tracked — hence the
	 * untrack / clear / re-track dance (mirrors msgspec's StructMeta_dealloc). */
	PyObject_GC_UnTrack(self);
	StructMeta_clear(self);
	PyObject_GC_Track(self);
	PyType_Type.tp_dealloc(self);
}

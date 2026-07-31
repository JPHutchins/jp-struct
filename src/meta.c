#include <Python.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "construct.h"
#include "fields.h"
#include "meta.h"
#include "mixin.h"
#include "options.h"
#include "owned.h"
#include "result.h"
#include "types.h"

#ifndef Py_TPFLAGS_HAVE_VECTORCALL
#	define Py_TPFLAGS_HAVE_VECTORCALL _Py_TPFLAGS_HAVE_VECTORCALL
#endif

/* Where type.__new__ placed the slot it created for a field name. */
struct member_lookup {
	enum { MEMBER_LOOKUP_FOUND, MEMBER_LOOKUP_MISSING, MEMBER_LOOKUP_ERROR } tag;
	Py_ssize_t offset;
};

static int StructMeta_traverse(PyObject * self, visitproc visit, void * arg);
static int StructMeta_clear(PyObject * self);
static void StructMeta_dealloc(PyObject * self);

static StructType * find_struct_base(PyObject * bases);
static bool has_weakref_slot(StructType const * base);
static bool inherits_body_eq(StructType const * base);
static PyObject * build_class_namespace(
	PyObject * original_namespace,
	PyObject * all_names,
	PyObject * new_names,
	struct options options,
	struct options inherited,
	StructType const * base
);
static PyObject * build_slots(PyObject * new_names, bool weakref);
static enum result set_match_args(PyObject * namespace, PyObject * all_names, bool wanted);
static enum result apply_options(
	PyObject * namespace,
	struct options options,
	struct options inherited,
	bool base_defines_eq
);
static enum result rebind(PyObject * namespace, char const * const * names, bool from_mixin);
static enum result bind_hash(
	PyObject * namespace,
	struct options options,
	struct options inherited,
	bool body_defines_eq,
	bool base_defines_eq
);
static enum result drop_class_variables(PyObject * namespace, PyObject * all_names);
static StructType * create_class(
	PyTypeObject * metatype,
	PyObject * name,
	PyObject * bases,
	PyObject * namespace
);
static enum result install_fields(
	StructType * struct_class,
	StructType const * base,
	struct field_plan const * plan,
	struct options options
);
static enum result install_post_init(StructType * struct_class);
static bool defines_own_init(StructType const * struct_class);
static PyObject * StructMeta_call(PyObject * self, PyObject * args, PyObject * keywords);
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
	PyVarObject_HEAD_INIT(NULL, 0)
	.tp_name = "salix.StructMeta",
	.tp_basicsize = sizeof(StructType),
	.tp_itemsize = sizeof(PyMemberDef),
	.tp_flags = (
		Py_TPFLAGS_DEFAULT |
		Py_TPFLAGS_TYPE_SUBCLASS |
		Py_TPFLAGS_HAVE_GC |
		Py_TPFLAGS_HAVE_VECTORCALL |
		Py_TPFLAGS_BASETYPE
	),
	.tp_new = StructMeta_new,
	.tp_dealloc = StructMeta_dealloc,
	.tp_traverse = StructMeta_traverse,
	.tp_clear = StructMeta_clear,
	.tp_call = StructMeta_call,
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
	struct options const inherited = base != NULL ? base->struct_options : options_initial();
	struct options_request const request =
		options_read(keywords, inherited, base != NULL && base->struct_field_count > 0);

	if (request.tag == OPTIONS_REJECTED) {
		return NULL;
	}

	struct field_plan plan = field_plan_build(base, original_namespace);

	if (field_plan_failed(&plan)) {
		return NULL;
	}

	PY_OWNED(
		namespace,
		build_class_namespace(
			original_namespace,
			plan.all_names,
			plan.new_names,
			request.options,
			inherited,
			base
		)
	);
	StructType * struct_class = (
		namespace != NULL ? create_class(metatype, name, bases, namespace) :
		NULL
	);

	if (
		struct_class != NULL &&
		install_fields(struct_class, base, &plan, request.options) != RESULT_OK
	) {
		Py_CLEAR(struct_class);
	}

	field_plan_clear(&plan);

	return (PyObject *) struct_class;
}

/* Find the (single) struct base among ``bases``. A fieldless one still carries
 * the options a subclass inherits, so it counts. */
static StructType * find_struct_base(PyObject * const bases) {
	for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); ++i) {
		PyObject * const base = PyTuple_GET_ITEM(bases, i);

		if (PyObject_TypeCheck(base, &StructMeta_Type)) {
			return (StructType *) base;
		}
	}

	return NULL;
}

/* CPython refuses a second __weakref__ in a subclass, so an inherited one is
 * what `weakref=True` already got. */
static bool has_weakref_slot(StructType const * const base) {
	return base != NULL && base->heap_type.ht_type.tp_weaklistoffset != 0;
}

/*
 * Whether this class will resolve an __eq__ that salix did not bind. salix
 * binds the mixin's or object's, so anything else came from a class body, and
 * Python's rule -- a body defining __eq__ and not __hash__ is unhashable --
 * reaches a subclass through the MRO even though its own body has neither.
 *
 * An unhashable tp_hash is the cheap gate, not the answer: a base is also
 * unhashable for being mutable with structural equality, and that reason does
 * not survive into a child that freezes itself, which a fieldless base permits.
 * The gate keeps the lookups off the common path, class creation being the
 * number this project is careful about; none of the three can fail, since
 * every type answers __eq__.
 */
static bool inherits_body_eq(StructType const * const base) {
	if (base == NULL || base->heap_type.ht_type.tp_hash != PyObject_HashNotImplemented) {
		return false;
	}

	PY_OWNED(inherited_eq, PyObject_GetAttrString((PyObject *) base, "__eq__"));
	PY_OWNED(mixin_eq, PyObject_GetAttrString((PyObject *) &StructMixin_Type, "__eq__"));
	PY_OWNED(object_eq, PyObject_GetAttrString((PyObject *) &PyBaseObject_Type, "__eq__"));

	/* Unreachable, and answered in the safe direction anyway: a class wrongly
	 * left unhashable says so the first time anyone hashes it, where one wrongly
	 * given a hash puts two equal instances in two slots of a set and says
	 * nothing. */
	if (inherited_eq == NULL || mixin_eq == NULL || object_eq == NULL) {
		PyErr_Clear();

		return true;
	}

	return inherited_eq != mixin_eq && inherited_eq != object_eq;
}

/* The namespace handed to type.__new__: a copy of the original with the
 * default-bearing field names removed (so __slots__ won't clash with a class
 * variable) plus __slots__ / __match_args__ and whatever the options replace. */
static PyObject * build_class_namespace(
	PyObject * const original_namespace,
	PyObject * const all_names,
	PyObject * const new_names,
	struct options const options,
	struct options const inherited,
	StructType const * const base
) {
	PY_OWNED(slots, build_slots(new_names, options.weakref && !has_weakref_slot(base)));
	PY_MOVABLE(namespace, PyDict_Copy(original_namespace));

	if (
		slots != NULL &&
		namespace != NULL &&
		drop_class_variables(namespace, all_names) == RESULT_OK &&
		PyDict_SetItemString(namespace, "__slots__", slots) == 0 &&
		set_match_args(namespace, all_names, options.match_args) == RESULT_OK &&
		apply_options(namespace, options, inherited, inherits_body_eq(base)) == RESULT_OK
	) {
		return py_move(&namespace);
	}

	return NULL;
}

/* __weakref__ is a slot like any other; a class that wants to be the target of
 * a weak reference asks for one. */
static PyObject * build_slots(PyObject * const new_names, bool const weakref) {
	PY_OWNED(names, PySequence_List(new_names));

	if (names == NULL) {
		return NULL;
	}

	if (weakref) {
		PY_OWNED(weakref_name, PyUnicode_FromString("__weakref__"));

		if (weakref_name == NULL || PyList_Append(names, weakref_name) < 0) {
			return NULL;
		}
	}

	return PyList_AsTuple(names);
}

/* Left unset rather than emptied, so a subclass that opts out still matches
 * positionally on whatever its base declared -- as a dataclass does. */
static enum result set_match_args(
	PyObject * const namespace,
	PyObject * const all_names,
	bool const wanted
) {
	if (!wanted) {
		return RESULT_OK;
	}

	PY_OWNED(match_args, PyList_AsTuple(all_names));

	return (
		match_args != NULL && PyDict_SetItemString(
			namespace,
			"__match_args__",
			match_args
		) == 0 ? RESULT_OK :
		RESULT_ERROR
	);
}

/*
 * An option is off when object answers the name and on when the mixin does, so
 * turning one on is as much work as turning it off: a subclass of a class that
 * opted out inherits that class's bindings, not the mixin's, and only an
 * explicit rebind gets the behaviour back.
 *
 * Bound in the namespace rather than written to the slots afterwards: the type
 * machinery derives tp_setattro, tp_richcompare, tp_hash and tp_repr from what
 * the class body defines, so assigning a slot directly would leave the dunder
 * still resolving one way while the operator took the other.
 */
static enum result apply_options(
	PyObject * const namespace,
	struct options const options,
	struct options const inherited,
	bool const base_defines_eq
) {
	/* Read before the rebinds below: afterwards every comparison name is
	 * present whether the body wrote one or salix did. */
	bool const body_defines_eq = PyDict_GetItemString(namespace, "__eq__") != NULL;

	/* All six, not just __eq__: they share tp_richcompare, and a class that
	 * rebinds only some of them gets the dispatching slot with the other source
	 * still answering the rest. */
	static char const * const comparison[] = {
		"__eq__",
		"__ne__",
		"__lt__",
		"__le__",
		"__gt__",
		"__ge__",
		NULL,
	};
	static char const * const representation[] = {"__repr__", NULL};
	static char const * const mutability[] = {"__setattr__", "__delattr__", NULL};

	if (options.eq != inherited.eq && rebind(namespace, comparison, options.eq) != RESULT_OK) {
		return RESULT_ERROR;
	}

	if (
		options.repr != inherited.repr &&
		rebind(namespace, representation, options.repr) != RESULT_OK
	) {
		return RESULT_ERROR;
	}

	if (
		options.frozen != inherited.frozen &&
		rebind(namespace, mutability, options.frozen) != RESULT_OK
	) {
		return RESULT_ERROR;
	}

	return bind_hash(namespace, options, inherited, body_defines_eq, base_defines_eq);
}

/* A name the class body defined is neither source's to take. */
static enum result rebind(
	PyObject * const namespace,
	char const * const * const names,
	bool const from_mixin
) {
	PyObject * const source = (
		from_mixin ? (PyObject *) &StructMixin_Type :
		(PyObject *) &PyBaseObject_Type
	);

	for (char const * const * name = names; *name != NULL; ++name) {
		if (PyDict_GetItemString(namespace, *name) != NULL) {
			continue;
		}

		PY_OWNED(bound, PyObject_GetAttrString(source, *name));

		if (bound == NULL || PyDict_SetItemString(namespace, *name, bound) < 0) {
			return RESULT_ERROR;
		}
	}

	return RESULT_OK;
}

/*
 * The hash follows from the other two answers rather than being an option of
 * its own: the tuple of the fields for a frozen value, object's identity hash
 * where equality is identity, and None for a value that compares by value and
 * can still move -- a key whose hash moves is not a key. Settled outright
 * rather than on a transition, because it is the one name two options answer.
 */
static enum result bind_hash(
	PyObject * const namespace,
	struct options const options,
	struct options const inherited,
	bool const body_defines_eq,
	bool const base_defines_eq
) {
	if (PyDict_GetItemString(namespace, "__hash__") != NULL) {
		return RESULT_OK;
	}

	/* Python's rule: a body that defines __eq__ and not __hash__ is unhashable.
	 * An inherited body __eq__ carries the same debt to a subclass whose own
	 * body has neither -- and only taking equality back, by writing __eq__ or
	 * by changing the eq option, replaces what the MRO would resolve. */
	if (
		body_defines_eq ||
		(options.eq && !options.frozen) ||
		(base_defines_eq && options.eq == inherited.eq)
	) {
		return PyDict_SetItemString(namespace, "__hash__", Py_None) == 0 ? RESULT_OK : RESULT_ERROR;
	}

	static char const * const hash_name[] = {"__hash__", NULL};

	return rebind(namespace, hash_name, options.eq);
}

/* Any class-body binding of a field name -- an annotated default, or a bare
 * assignment over a name the base already declared -- would sit in this class's
 * dict ahead of the __slots__ descriptor that reads the value. */
static enum result drop_class_variables(PyObject * const namespace, PyObject * const all_names) {
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(all_names); ++i) {
		PyObject * const field_name = PyList_GET_ITEM(all_names, i);
		int const present = PyDict_Contains(namespace, field_name);

		if (present < 0 || (present == 1 && PyDict_DelItem(namespace, field_name) < 0)) {
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
	struct field_plan const * const plan,
	struct options const options
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
	struct_class->struct_options = options;

	/* The mixin has no tp_new, because nothing ever needed one: the vectorcall
	 * allocates. A class that declined it needs the generic one to get as far
	 * as its own __init__ -- and only such a class, so the mixin itself stays
	 * uninstantiable and nothing can hold a struct's dunders over an object
	 * that has no field table. */
	if (defines_own_init(struct_class)) {
		struct_class->heap_type.ht_type.tp_new = PyType_GenericNew;
	} else {
		struct_class->heap_type.ht_type.tp_vectorcall = Struct_vectorcall;
	}

	return install_post_init(struct_class);
}

/*
 * A body that writes its own __init__ means it. The generated constructor is
 * what a struct gets, not what it is stuck with, and leaving the vectorcall
 * installed would discard the definition in silence -- tp_call never reaches
 * tp_init once tp_vectorcall answers.
 *
 * tp_init rather than a lookup: it is object's until something in the MRO
 * defines __init__, at which point the type machinery has already replaced it
 * with the dispatching slot. That covers an inherited one for free.
 */
static bool defines_own_init(StructType const * const struct_class) {
	return struct_class->heap_type.ht_type.tp_init != PyBaseObject_Type.tp_init;
}

/* PyVectorcall_Call cannot answer for the classes that just declined the
 * vectorcall, and type.__call__ is what they want anyway. */
static PyObject * StructMeta_call(
	PyObject * const self,
	PyObject * const args,
	PyObject * const keywords
) {
	return (
		((PyTypeObject *) self)->tp_vectorcall != NULL ? PyVectorcall_Call(self, args, keywords) :
		PyType_Type.tp_call(self, args, keywords)
	);
}

/*
 * Resolved once, here, rather than looked up per construction: the constructor
 * writes slots and returns, and an MRO walk on every instance would be the
 * largest thing in it. The cost is that a __post_init__ bound to the class
 * after it exists is not seen.
 */
static enum result install_post_init(StructType * const struct_class) {
	PyObject * const hook = PyObject_GetAttrString((PyObject *) struct_class, "__post_init__");

	if (hook != NULL) {
		struct_class->struct_post_init = hook;

		return RESULT_OK;
	}

	if (!PyErr_ExceptionMatches(PyExc_AttributeError)) {
		return RESULT_ERROR;
	}

	PyErr_Clear();

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
			case MEMBER_LOOKUP_ERROR:
				PyMem_Free(offsets);
				return NULL;
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
	Py_ssize_t name_size = 0;
	char const * const encoded_name = PyUnicode_AsUTF8AndSize(name, &name_size);

	if (encoded_name == NULL) {
		return (struct member_lookup){.tag = MEMBER_LOOKUP_ERROR};
	}

	for (Py_ssize_t i = 0; i < member_count; ++i) {
		size_t const member_size = strlen(members[i].name);

		if (
			name_size == (Py_ssize_t) member_size &&
			memcmp(encoded_name, members[i].name, member_size) == 0
		) {
			return (struct member_lookup){.tag = MEMBER_LOOKUP_FOUND, .offset = members[i].offset};
		}
	}

	return (struct member_lookup){.tag = MEMBER_LOOKUP_MISSING};
}

/* `visit` and `arg` are not free names: Py_VISIT expands to reference both by
 * those exact spellings, so renaming either one stops the macro compiling. */
static int StructMeta_traverse(PyObject * const self, visitproc const visit, void * const arg) {
	StructType * const struct_class = (StructType *) self;

	Py_VISIT(struct_class->struct_field_names);
	Py_VISIT(struct_class->struct_defaults);
	Py_VISIT(struct_class->struct_post_init);

	return PyType_Type.tp_traverse(self, visit, arg);
}

static int StructMeta_clear(PyObject * const self) {
	StructType * const struct_class = (StructType *) self;

	/* Ahead of the guard: a class whose creation failed after the hook was
	 * resolved has one to drop and no fields. */
	Py_CLEAR(struct_class->struct_post_init);

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
	{.name = "café", .offset = 32},
	{.name = NULL},
};

static void test_a_declared_member_yields_its_offset(void) {
	PyObject * const name = PyUnicode_FromString("beta");
	struct member_lookup const found = find_member(example_members, 2, name);

	TEST_ASSERT_EQUAL_INT(MEMBER_LOOKUP_FOUND, found.tag);
	TEST_ASSERT_EQUAL_INT(24, found.offset);

	Py_DECREF(name);
}

static void test_a_non_ascii_member_yields_its_offset(void) {
	PyObject * const name = PyUnicode_FromString("café");
	struct member_lookup const found = find_member(example_members, 3, name);

	TEST_ASSERT_EQUAL_INT(MEMBER_LOOKUP_FOUND, found.tag);
	TEST_ASSERT_EQUAL_INT(32, found.offset);

	Py_DECREF(name);
}

static void test_an_undeclared_member_is_missing(void) {
	PyObject * const name = PyUnicode_FromString("gamma");
	struct member_lookup const found = find_member(example_members, 3, name);

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
	RUN_TEST(test_a_non_ascii_member_yields_its_offset);
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

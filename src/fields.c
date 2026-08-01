#include <Python.h>
#include <stdbool.h>

#include "annotations.h"
#include "construct.h"
#include "fields.h"
#include "owned.h"
#include "result.h"
#include "types.h"

enum inheritance : int {
	INHERITANCE_ERROR = -1,
	INHERITANCE_NEW = 0,
	INHERITANCE_INHERITED = 1,
};

static enum result append_inherited(
	StructType const * base,
	PyObject * all_names,
	PyObject * default_by_name
);
static enum result append_declared(
	StructType const * base,
	PyObject * annotations,
	PyObject * namespace,
	PyObject * all_names,
	PyObject * new_names,
	PyObject * default_by_name
);
static PyObject * build_defaults(PyObject * all_names, PyObject * default_by_name);
static enum result reject_unsafe_default(PyObject * field_name, PyObject * value);
static PyObject * checked_annotations(PyObject * namespace);
static enum inheritance inherits_field(StructType const * base, PyObject * field_name);

/* The plan only takes references once every step has succeeded; the working
 * collections belong to this scope either way. */
struct field_plan field_plan_build(StructType const * const base, PyObject * const namespace) {
	struct field_plan plan = {0};

	PY_OWNED(annotations, checked_annotations(namespace));

	if (annotations == NULL) {
		return plan;
	}

	PY_MOVABLE(all_names, PyList_New(0));
	PY_MOVABLE(new_names, PyList_New(0));
	PY_OWNED(default_by_name, PyDict_New());

	if (
		all_names != NULL &&
		new_names != NULL &&
		default_by_name != NULL &&
		append_inherited(base, all_names, default_by_name) == RESULT_OK &&
		append_declared(base, annotations, namespace, all_names, new_names, default_by_name) ==
			RESULT_OK
	) {
		plan.defaults = build_defaults(all_names, default_by_name);

		if (plan.defaults != NULL) {
			plan.all_names = py_move(&all_names);
			plan.new_names = py_move(&new_names);
		}
	}

	return plan;
}

void field_plan_clear(struct field_plan * const plan) {
	Py_CLEAR(plan->all_names);
	Py_CLEAR(plan->new_names);
	Py_CLEAR(plan->defaults);
}

static PyObject * checked_annotations(PyObject * const namespace) {
	PY_MOVABLE(annotations, struct_annotations(namespace));

	if (annotations == NULL || PyDict_Check(annotations)) {
		return py_move(&annotations);
	}

	PyErr_SetString(PyExc_TypeError, "__annotations__ must be a dict");

	return NULL;
}

/* Inherited fields keep their position and defaults. */
static enum result append_inherited(
	StructType const * const base,
	PyObject * const all_names,
	PyObject * const default_by_name
) {
	if (base == NULL) {
		return RESULT_OK;
	}

	Py_ssize_t const required_count = struct_required_count(base);

	for (Py_ssize_t i = 0; i < base->struct_field_count; ++i) {
		PyObject * const field_name = PyTuple_GET_ITEM(base->struct_field_names, i);

		if (PyList_Append(all_names, field_name) < 0) {
			return RESULT_ERROR;
		}

		if (i < required_count) {
			continue;
		}

		PyObject * const inherited_default =
			PyTuple_GET_ITEM(base->struct_defaults, i - required_count);

		if (PyDict_SetItem(default_by_name, field_name, inherited_default) < 0) {
			return RESULT_ERROR;
		}
	}

	return RESULT_OK;
}

/* New fields come from this class's annotations, in declaration order. */
static enum result append_declared(
	StructType const * const base,
	PyObject * const annotations,
	PyObject * const namespace,
	PyObject * const all_names,
	PyObject * const new_names,
	PyObject * const default_by_name
) {
	PyObject * field_name;
	PyObject * annotation;
	Py_ssize_t position = 0;

	while (PyDict_Next(annotations, &position, &field_name, &annotation)) {
		if (!PyUnicode_CheckExact(field_name)) {
			PyErr_SetString(PyExc_TypeError, "annotation keys must be strings");

			return RESULT_ERROR;
		}

		/* A default is the class-body value bound to the field name. */
		PyObject * const declared_default = PyDict_GetItem(namespace, field_name);

		if (
			declared_default != NULL &&
			PyDict_SetItem(default_by_name, field_name, declared_default) < 0
		) {
			return RESULT_ERROR;
		}

		/* Skip if this name was already inherited (override of annotation, not
		 * a new slot). */
		switch (inherits_field(base, field_name)) {
			case INHERITANCE_ERROR:
				return RESULT_ERROR;
			case INHERITANCE_INHERITED:
				continue;
			case INHERITANCE_NEW:
				break;
		}

		if (PyList_Append(all_names, field_name) < 0 || PyList_Append(new_names, field_name) < 0) {
			return RESULT_ERROR;
		}
	}

	return RESULT_OK;
}

/* PyObject_RichCompareBool answers -1 for an error and nothing else, so the
 * three cases are the return value itself -- no PyErr_Occurred() to tell a
 * failure apart from a "less than", and so no invariant about the exception
 * state on entry. The same tri-state compare.c reads into `enum comparison`. */
static enum inheritance inherits_field(StructType const * const base, PyObject * const field_name) {
	Py_ssize_t const inherited_count = base != NULL ? base->struct_field_count : 0;

	for (Py_ssize_t i = 0; i < inherited_count; ++i) {
		enum inheritance const inherited = PyObject_RichCompareBool(
			field_name,
			PyTuple_GET_ITEM(base->struct_field_names, i),
			Py_EQ
		);

		if (inherited != INHERITANCE_NEW) {
			return inherited;
		}
	}

	return INHERITANCE_NEW;
}

/*
 * An empty mutable container is copied per instance, so it means what it looks
 * like it means. A non-empty one cannot be: copying it is necessarily shallow,
 * and `xs: list = [[1]]` would hand every instance its own outer list around
 * the *same* inner one -- an aliasing bug one level down from the one being
 * fixed. Refused instead, which is what msgspec does and for the same reason.
 *
 * Only the exact builtins, because the copy has to preserve the type and
 * PyDict_Copy of a defaultdict is a dict. A subclass is shared, as it is
 * there.
 */
static enum result reject_unsafe_default(PyObject * const field_name, PyObject * const value) {
	PyTypeObject * const kind = Py_TYPE(value);
	Py_ssize_t const filled = (
		kind == &PyList_Type || kind == &PyDict_Type || kind == &PySet_Type || kind == &PyByteArray_Type ? PyObject_Size(
			value
		) :
		0
	);

	if (filled <= 0) {
		return filled < 0 ? RESULT_ERROR : RESULT_OK;
	}

	PyErr_Format(
		PyExc_TypeError,
		"field '%U' defaults to a non-empty %.100s, which every instance would "
		"share the contents of; default it to an empty one and fill it from "
		"__post_init__, or from your own __init__, with set_field",
		field_name,
		kind->tp_name
	);

	return RESULT_ERROR;
}

/* Build the defaults tuple as the trailing run of defaulted fields, and
 * enforce that no required field follows a defaulted one (same rule as
 * Python function signatures). */
static PyObject * build_defaults(PyObject * const all_names, PyObject * const default_by_name) {
	Py_ssize_t const field_count = PyList_GET_SIZE(all_names);
	Py_ssize_t first_default = field_count;

	for (Py_ssize_t i = 0; i < field_count; ++i) {
		PyObject * const field_name = PyList_GET_ITEM(all_names, i);
		int const has_default = PyDict_Contains(default_by_name, field_name);

		if (has_default < 0) {
			return NULL;
		}

		if (has_default) {
			first_default = first_default == field_count ? i : first_default;
		} else if (first_default != field_count) {
			PyErr_Format(
				PyExc_TypeError,
				"non-default field '%U' follows a field with a default",
				field_name
			);

			return NULL;
		}
	}

	PyObject * const defaults = PyTuple_New(field_count - first_default);

	if (defaults == NULL) {
		return NULL;
	}

	for (Py_ssize_t i = first_default; i < field_count; ++i) {
		PyObject * const field_name = PyList_GET_ITEM(all_names, i);
		PyObject * const value = PyDict_GetItem(default_by_name, field_name);

		/* Copy first and check the copy, not the declaration. The two are
		 * separate reads of an object the module still holds, and on a
		 * free-threaded build another thread can fill it in between -- so what
		 * is checked has to be the thing that is kept. Once kept it is the
		 * class's own and nobody can fill it, which is what makes the emptiness
		 * permanent rather than momentary. */
		PyObject * const stored = struct_default_copy(value);

		if (stored == NULL || reject_unsafe_default(field_name, stored) != RESULT_OK) {
			Py_XDECREF(stored);
			Py_DECREF(defaults);

			return NULL;
		}

		PyTuple_SET_ITEM(defaults, i - first_default, stored);
	}

	return defaults;
}

#ifdef TESTING

#	include "testing.h"

/* build_defaults takes plain lists and dicts, so it is testable without a class
 * -- and the ordering rule is easier to state here than through a class body. */
static PyObject * names_of(char const * const * const names, Py_ssize_t const count) {
	PyObject * const list = PyList_New(0);

	for (Py_ssize_t i = 0; i < count; ++i) {
		PyObject * const name = PyUnicode_FromString(names[i]);

		PyList_Append(list, name);
		Py_DECREF(name);
	}

	return list;
}

static PyObject * defaults_for(char const * const * const names, Py_ssize_t const count) {
	PyObject * const mapping = PyDict_New();

	for (Py_ssize_t i = 0; i < count; ++i) {
		PyObject * const value = PyLong_FromSsize_t(i);

		PyDict_SetItemString(mapping, names[i], value);
		Py_DECREF(value);
	}

	return mapping;
}

static void test_no_defaults_produces_an_empty_tuple(void) {
	char const * const names[] = {"a", "b"};
	PyObject * const all_names = names_of(names, 2);
	PyObject * const by_name = defaults_for(names, 0);
	PyObject * const defaults = build_defaults(all_names, by_name);

	TEST_ASSERT_NOT_NULL(defaults);
	TEST_ASSERT_EQUAL_INT(0, PyTuple_GET_SIZE(defaults));

	Py_DECREF(defaults);
	Py_DECREF(by_name);
	Py_DECREF(all_names);
}

static void test_only_the_trailing_run_becomes_defaults(void) {
	char const * const names[] = {"a", "b", "c"};
	char const * const defaulted[] = {"b", "c"};
	PyObject * const all_names = names_of(names, 3);
	PyObject * const by_name = defaults_for(defaulted, 2);
	PyObject * const defaults = build_defaults(all_names, by_name);

	TEST_ASSERT_NOT_NULL(defaults);
	TEST_ASSERT_EQUAL_INT(2, PyTuple_GET_SIZE(defaults));

	Py_DECREF(defaults);
	Py_DECREF(by_name);
	Py_DECREF(all_names);
}

static void test_a_required_field_after_a_default_is_rejected(void) {
	char const * const names[] = {"a", "b"};
	char const * const defaulted[] = {"a"};
	PyObject * const all_names = names_of(names, 2);
	PyObject * const by_name = defaults_for(defaulted, 1);
	PyObject * const defaults = build_defaults(all_names, by_name);

	TEST_ASSERT_NULL(defaults);
	TEST_ASSERT_TRUE(PyErr_ExceptionMatches(PyExc_TypeError));

	PyErr_Clear();
	Py_DECREF(by_name);
	Py_DECREF(all_names);
}

void fields_tests(void) {
	/* Unity takes its file from UNITY_BEGIN, which is the runner's. */
	Unity.TestFile = __FILE__;

	RUN_TEST(test_no_defaults_produces_an_empty_tuple);
	RUN_TEST(test_only_the_trailing_run_becomes_defaults);
	RUN_TEST(test_a_required_field_after_a_default_is_rejected);
}

#endif

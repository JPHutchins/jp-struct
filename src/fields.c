#include <Python.h>
#include <stdbool.h>
#include <string.h>

#include "annotations.h"
#include "fields.h"
#include "owned.h"
#include "result.h"
#include "types.h"

/* An annotation naming something that is not a field, and what to do instead. */
struct special_form {
	char const * name;
	char const * instead;
};

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
static PyObject * checked_annotations(PyObject * namespace);
static enum inheritance inherits_field(StructType const * base, PyObject * field_name);
static struct special_form special_form_of(
	PyObject * annotation,
	PyObject * class_var,
	PyObject * init_var
);
static struct special_form named_special_form(PyObject * text);
static bool names_form(char const * source, char const * form);
static PyObject * module_attribute(char const * module_name, char const * attribute);

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

	/* Once per class, not once per field. Absent means the module was never
	 * imported, so nothing in this body can be naming what it holds. */
	PY_OWNED(class_var, module_attribute("typing", "ClassVar"));
	PY_OWNED(init_var, module_attribute("dataclasses", "InitVar"));

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

		/* After the inheritance check, so re-annotating an inherited field is
		 * the no-op it has always been rather than a new refusal. */
		struct special_form const special = special_form_of(annotation, class_var, init_var);

		if (special.name != NULL) {
			PyErr_Format(
				PyExc_TypeError,
				"'%U' is annotated %s, which salix does not support; %s",
				field_name,
				special.name,
				special.instead
			);

			return RESULT_ERROR;
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
 * ClassVar and InitVar name something that is not a field, and a checker
 * reading the stub already knows it -- dataclass_transform excludes both. Left
 * alone they become fields, so `registry: ClassVar[int] = 0` swallows the first
 * positional argument and checked code and running code disagree about what it
 * means. Refused instead, until there is a way to ask for them.
 *
 * The __origin__ chain is walked rather than probed once, because
 * Annotated[ClassVar[int], ...] reaches ClassVar two hops down and is otherwise
 * the same bug wearing a wrapper. Bounded, so a self-referential __origin__
 * cannot spin here.
 */
enum : int {
	SPECIAL_FORM_HOPS = 4,
};

static struct special_form special_form_of(
	PyObject * const annotation,
	PyObject * const class_var,
	PyObject * const init_var
) {
	if (PyUnicode_Check(annotation)) {
		return named_special_form(annotation);
	}

	PyObject * current = annotation;

	PY_MOVABLE(held, NULL);

	for (int hop = 0; hop < SPECIAL_FORM_HOPS; ++hop) {
		if (class_var != NULL && current == class_var) {
			return (struct special_form){
				.name = "ClassVar",
				.instead = "write it below the fields, without an annotation",
			};
		}

		if (
			init_var != NULL &&
			(current == init_var || (PyObject *) Py_TYPE(current) == init_var)
		) {
			return (struct special_form){
				.name = "InitVar",
				.instead = "take the value in a custom __init__ and write the fields with set_field",
			};
		}

		/* A plain class is the common annotation and has no __origin__; asking
		 * anyway costs an AttributeError raised and cleared per field. */
		if (PyType_Check(current)) {
			break;
		}

		PyObject * const next = PyObject_GetAttrString(current, "__origin__");

		if (next == NULL) {
			PyErr_Clear();

			break;
		}

		Py_XSETREF(held, next);
		current = held;
	}

	return (struct special_form){0};
}

/*
 * Under `from __future__ import annotations` the annotation is its own source
 * text, so the only thing left to match is the spelling. A module alias is
 * covered, since `t.ClassVar[int]` still ends in the form after a dot; a
 * *renamed* import is not -- `from typing import ClassVar as CV` gives
 * `CV[int]`, which resolves to nothing here and becomes a field, as it did
 * before any of this. dataclasses guesses at those against sys.modules; this
 * does not.
 */
static struct special_form named_special_form(PyObject * const text) {
	char const * const source = PyUnicode_AsUTF8(text);

	if (source == NULL) {
		PyErr_Clear();

		return (struct special_form){0};
	}

	if (names_form(source, "ClassVar")) {
		return (struct special_form){
			.name = "ClassVar",
			.instead = "write it below the fields, without an annotation",
		};
	}

	if (names_form(source, "InitVar")) {
		return (struct special_form){
			.name = "InitVar",
			.instead = "take the value in a custom __init__ and write the fields with set_field",
		};
	}

	return (struct special_form){0};
}

/*
 * The form standing on its own somewhere in the text: at the start, after a dot
 * for a module alias, or inside a subscript for `Annotated[ClassVar[int], ...]`.
 * Not MyClassVar, and not ClassVarish.
 *
 * A heuristic in both directions, and the only thing available once the
 * annotation is source text. It misses a renamed import -- `ClassVar as CV`
 * gives `CV[int]` -- and it refuses a user's own type that happens to be called
 * ClassVar. The object path, which is what runs unless the module asked for
 * `from __future__ import annotations`, is exact and has neither problem.
 */
static bool names_form(char const * const source, char const * const form) {
	size_t const length = strlen(form);

	for (
		char const * found = strstr(source, form);
		found != NULL;
		found = strstr(found + 1, form)
	) {
		bool const opens = (
			found == source ||
			found[-1] == '.' ||
			found[-1] == '[' ||
			found[-1] == ' '
		);
		char const after = found[length];

		if (opens && (after == '\0' || after == '[' || after == ',' || after == ']')) {
			return true;
		}
	}

	return false;
}

/*
 * The attribute if its module is already loaded, and NULL if it is not --
 * without importing, which is the whole point. A new reference, so the caller
 * owns it: returning a borrowed one out of a PY_OWNED scope is the shape
 * owned.h warns about, even where the module would have kept it alive.
 */
static PyObject * module_attribute(char const * const module_name, char const * const attribute) {
	PY_OWNED(name, PyUnicode_FromString(module_name));

	if (name == NULL) {
		PyErr_Clear();

		return NULL;
	}

	PY_OWNED(module, PyImport_GetModule(name));

	if (module == NULL) {
		PyErr_Clear();

		return NULL;
	}

	PyObject * const found = PyObject_GetAttrString(module, attribute);

	if (found == NULL) {
		PyErr_Clear();
	}

	return found;
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
		PyObject * const value = PyDict_GetItem(default_by_name, PyList_GET_ITEM(all_names, i));

		PyTuple_SET_ITEM(defaults, i - first_default, Py_NewRef(value));
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

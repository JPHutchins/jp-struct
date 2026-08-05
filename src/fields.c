#include <Python.h>
#include <stdbool.h>

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

/* What the two paths match an annotation against. Built once per class, because
 * the text path's needles are the same two words for every field in it. */
struct form_probes {
	PyObject * class_var;
	PyObject * init_var;
	PyObject * class_var_name;
	PyObject * init_var_name;
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

/* Both paths answer the same question and owe the user the same sentence, so
 * the answers live here rather than once per path -- and the text path's
 * needles are built from these names, so each form is spelled once. */
static struct special_form const CLASS_VAR_FORM = {
	.name = "ClassVar",
	.instead = "write it below the fields, without an annotation",
};
static struct special_form const INIT_VAR_FORM = {
	.name = "InitVar",
	.instead = "take the value in a custom __init__ and write the fields with set_field",
};
static PyObject * build_defaults(PyObject * all_names, PyObject * default_by_name);
static PyObject * checked_annotations(PyObject * namespace);
static enum inheritance inherits_field(StructType const * base, PyObject * field_name);
static struct special_form special_form_of(
	PyObject * annotation,
	struct form_probes const * probes
);
static struct special_form form_within(PyObject * annotation, struct form_probes const * probes);
static PyObject * optional_attribute(PyObject * object, char const * name);
static struct special_form named_special_form(PyObject * text, struct form_probes const * probes);
static bool annotates_with_text(PyObject * annotations);
static bool names_form(PyObject * text, PyObject * needle);
static bool continues_identifier(Py_UCS4 character);
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
	/* A class with no annotations of its own declares no fields and so can name
	 * no forms; the four probes below are the whole cost of asking. */
	if (PyDict_GET_SIZE(annotations) == 0) {
		return RESULT_OK;
	}

	/* Once per class, not once per field. Absent means the module was never
	 * imported, so nothing in this body can be naming what it holds -- but
	 * absent and failed both come back NULL, and only the exception tells them
	 * apart. Failing here has to fail the class rather than quietly leave it
	 * unguarded. */
	PY_OWNED(class_var, module_attribute("typing", "ClassVar"));

	if (class_var == NULL && PyErr_Occurred()) {
		return RESULT_ERROR;
	}

	PY_OWNED(init_var, module_attribute("dataclasses", "InitVar"));

	if (init_var == NULL && PyErr_Occurred()) {
		return RESULT_ERROR;
	}

	/* From the same two strings the refusal quotes, so there is one spelling of
	 * each form in the file. Built here and not in the matcher, which runs per
	 * field and per form -- and only for a class that has text to match, since
	 * building them for every class taxes the ones that never look. */
	PY_MOVABLE(class_var_name, NULL);
	PY_MOVABLE(init_var_name, NULL);

	if (annotates_with_text(annotations)) {
		class_var_name = PyUnicode_FromString(CLASS_VAR_FORM.name);
		init_var_name = PyUnicode_FromString(INIT_VAR_FORM.name);

		if (class_var_name == NULL || init_var_name == NULL) {
			return RESULT_ERROR;
		}
	}

	struct form_probes const probes = {
		.class_var = class_var,
		.init_var = init_var,
		.class_var_name = class_var_name,
		.init_var_name = init_var_name,
	};

	/* The names are taken first, because the object path asks the annotation
	 * for `__origin__` and `__args__` and that runs the class author's code --
	 * which can add to or delete from this very dict. PyDict_Next's cursor and
	 * the pointers it hands back are only defined while the dict is unmodified,
	 * and one list per class is a cheap way not to depend on which mutations
	 * CPython happens to survive. */
	PY_OWNED(declared, PyDict_Keys(annotations));

	if (declared == NULL) {
		return RESULT_ERROR;
	}

	for (Py_ssize_t at = 0; at < PyList_GET_SIZE(declared); ++at) {
		PyObject * const field_name = PyList_GET_ITEM(declared, at);

		/* Held, because a later annotation's `__getattr__` may have deleted this
		 * one between the snapshot and here -- and gone is not a field. */
		PY_OWNED(annotation, Py_XNewRef(PyDict_GetItem(annotations, field_name)));

		if (annotation == NULL) {
			continue;
		}

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
		struct special_form const special = special_form_of(annotation, &probes);

		/* Before the answer is used at all, not only when it is "no form": the
		 * text path allocates on the way to either verdict, and a failure there
		 * must not be overwritten by a refusal that happens to agree. */
		if (PyErr_Occurred()) {
			return RESULT_ERROR;
		}

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
	/* A budget on work rather than on depth. The walk is a tree since #57's
	 * ruling, and bounding the shape no longer bounds the effort: four hops was
	 * enough for a chain -- Optional[Annotated[ClassVar[int], 'm']] is four --
	 * and this is enough for the arguments beside them, while a
	 * self-referential __origin__ still costs a fixed amount. */
	SPECIAL_FORM_NODES = 32,
};

/*
 * Owned, every one of them. __origin__ hands back a new reference, and an
 * argument outlives the tuple it was read from only because this holds it.
 *
 * A fixed array rather than a list, because the frontier is bounded anyway and
 * an allocation here would be one per subscripted annotation per class -- the
 * cost the needles were just moved out of the matcher to avoid.
 */
struct form_frontier {
	PyObject * nodes[SPECIAL_FORM_NODES];
	int count;
};

static void form_frontier_clear(struct form_frontier * const frontier) {
	while (frontier->count > 0) {
		Py_DECREF(frontier->nodes[--frontier->count]);
	}
}

/* Full is not a failure: the budget is the point, and a walk that runs out
 * answers "no form", which is what an unrecognised annotation answers. */
static void form_frontier_push(struct form_frontier * const frontier, PyObject * const node) {
	if (frontier->count < SPECIAL_FORM_NODES) {
		frontier->nodes[frontier->count++] = Py_NewRef(node);
	}
}

static struct special_form form_named_by(
	PyObject * const annotation,
	struct form_probes const * const probes
) {
	if (probes->class_var != NULL && annotation == probes->class_var) {
		return CLASS_VAR_FORM;
	}

	if (
		probes->init_var != NULL &&
		(annotation == probes->init_var || (PyObject *) Py_TYPE(annotation) == probes->init_var)
	) {
		return INIT_VAR_FORM;
	}

	return (struct special_form){0};
}

static struct special_form special_form_of(
	PyObject * const annotation,
	struct form_probes const * const probes
) {
	if (PyUnicode_Check(annotation)) {
		return named_special_form(annotation, probes);
	}

	/* Neither module is loaded, so nothing reachable from here can be either
	 * form and the walk below can only ask attributes of things for nothing. */
	if (probes->class_var == NULL && probes->init_var == NULL) {
		return (struct special_form){0};
	}

	return form_within(annotation, probes);
}

/*
 * The form can be anywhere in a subscripted annotation, not only at the end of
 * the __origin__ chain. `Optional[Annotated[ClassVar[int], 'm']]` keeps it in
 * the arguments, where a chain walk never looks, so it became a field while
 * the text path refused the same source -- #14 again, on the path almost every
 * class takes. Both are walked now, per #57's ruling.
 *
 * It also makes the object path refuse `Annotated[int, ClassVar]`, where the
 * form is metadata rather than the type. That is not a new refusal: the text
 * path already refuses it and ships that way, and one answer is worth more
 * than two that disagree.
 *
 * A str inside __args__ is walked, not matched -- `Annotated[int, "ClassVar"]`
 * is a string in a metadata slot, and the text matcher is for an annotation
 * that reached salix as source, not for anything that looks like one.
 *
 * A queue rather than a recursion, because clang-tidy's misc-no-recursion is an
 * error here and the shape does not need one: the frontier is what bounds the
 * walk, so neither a self-referential __origin__ nor an __args__ holding its
 * own owner can spin.
 */
static struct special_form form_within(
	PyObject * const annotation,
	struct form_probes const * const probes
) {
	__attribute__((cleanup(form_frontier_clear))) struct form_frontier frontier = {0};

	form_frontier_push(&frontier, annotation);

	for (int at = 0; at < frontier.count; ++at) {
		PyObject * const current = frontier.nodes[at];
		struct special_form const named = form_named_by(current, probes);

		if (named.name != NULL) {
			return named;
		}

		/* A plain class is the common annotation and has neither attribute;
		 * asking anyway costs two AttributeErrors raised and cleared. */
		if (PyType_Check(current)) {
			continue;
		}

		PY_OWNED(origin, optional_attribute(current, "__origin__"));

		if (origin != NULL) {
			form_frontier_push(&frontier, origin);
		}

		PY_OWNED(arguments, PyErr_Occurred() ? NULL : optional_attribute(current, "__args__"));

		if (PyErr_Occurred()) {
			return (struct special_form){0};
		}

		for (
			Py_ssize_t i = 0;
			arguments != NULL && PyTuple_Check(arguments) && i < PyTuple_GET_SIZE(arguments);
			++i
		) {
			form_frontier_push(&frontier, PyTuple_GET_ITEM(arguments, i));
		}
	}

	return (struct special_form){0};
}

/* The attribute if it is there, and NULL if it is not. NULL with an exception
 * set is a failure to look, which the caller propagates rather than clears --
 * a guard that cannot see is not a guard that says "ordinary field". */
static PyObject * optional_attribute(PyObject * const object, char const * const name) {
	PyObject * const value = PyObject_GetAttrString(object, name);

	if (value == NULL && PyErr_ExceptionMatches(PyExc_AttributeError)) {
		PyErr_Clear();
	}

	return value;
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
/* The same question the matcher asks per field, asked once for the class: is
 * there any text here at all? Under `from __future__ import annotations` every
 * annotation is, and otherwise almost none are. */
static bool annotates_with_text(PyObject * const annotations) {
	PyObject * field_name;
	PyObject * annotation;
	Py_ssize_t position = 0;

	while (PyDict_Next(annotations, &position, &field_name, &annotation)) {
		if (PyUnicode_Check(annotation)) {
			return true;
		}
	}

	return false;
}

static struct special_form named_special_form(
	PyObject * const text,
	struct form_probes const * const probes
) {
	if (names_form(text, probes->class_var_name)) {
		return CLASS_VAR_FORM;
	}

	if (names_form(text, probes->init_var_name)) {
		return INIT_VAR_FORM;
	}

	return (struct special_form){0};
}

/*
 * The form standing on its own somewhere in the text -- at the start, after a
 * dot for a module alias, inside a subscript for `Annotated[ClassVar[int], ...]`
 * -- rather than as part of a longer name. Not MyClassVar, and not ClassVarish.
 *
 * The boundary is "no identifier character adjacent" rather than a list of the
 * punctuation seen so far. Enumerating openers and closers separately is how
 * `ClassVar ` and `ClassVar [int]`, both legal and both stored verbatim under
 * future annotations, walked past an earlier version of this.
 *
 * Characters rather than UTF-8 bytes, and Python's own identifier rule rather
 * than a hand-written one. PEP 3131 lets a name hold any Unicode letter, so an
 * ASCII-only test read `théClassVar` as the form standing alone; calling every
 * byte at or above 0x80 an identifier character fixed that and broke the other
 * direction, since `€` is not one. Working on the str also means a lone
 * surrogate or an embedded NUL is nothing special -- neither has to survive an
 * encode that the guard would otherwise fail open on.
 *
 * Nothing here clears an error it did not expect. A failed allocation would
 * otherwise answer "not a form", which reads as "this is a field" -- the same
 * fail-open module_attribute had, in the path that decides the same question.
 *
 * A heuristic in both directions, and the only thing available once the
 * annotation is source text. It misses a renamed import -- `ClassVar as CV`
 * gives `CV[int]` -- and it refuses a user's own type that happens to be called
 * ClassVar, and `Annotated[int, ClassVar]` where the form is metadata rather
 * than the type -- including when it is quoted, since a quote is a boundary.
 * That last one is deliberate: `x: 'ClassVar[int]'` is a nested forward
 * reference and has to be refused, and nothing short of parsing tells the two
 * apart. #57 keeps the list. The object path is exact, and it is what
 * runs unless the module asked for `from __future__ import annotations`.
 */
static bool continues_identifier(Py_UCS4 const character) {
	/* Python owns the answer and spells it one way in the C API: a name is an
	 * identifier when its first character starts one and the rest continue one,
	 * so "a" followed by this character asks about exactly this character. */
	PY_OWNED(probe, PyUnicode_New(2, character > 'a' ? character : 'a'));

	if (
		probe == NULL ||
		PyUnicode_WriteChar(probe, 0, 'a') < 0 ||
		PyUnicode_WriteChar(probe, 1, character) < 0
	) {
		return false;
	}

	return PyUnicode_IsIdentifier(probe) == 1;
}

static bool names_form(PyObject * const text, PyObject * const needle) {
	Py_ssize_t const length = PyUnicode_GET_LENGTH(text);
	Py_ssize_t const form_length = PyUnicode_GET_LENGTH(needle);

	for (Py_ssize_t at = 0; at + form_length <= length; ++at) {
		Py_ssize_t const found = PyUnicode_Find(text, needle, at, length, 1);

		if (found < 0) {
			return false;
		}

		bool const opens = found == 0 || !continues_identifier(PyUnicode_ReadChar(text, found - 1));
		bool const closes = (
			found + form_length == length ||
			!continues_identifier(PyUnicode_ReadChar(text, found + form_length))
		);

		if (opens && closes) {
			return true;
		}

		at = found;
	}

	return false;
}

/*
 * The attribute if its module is already loaded, and NULL if it is not --
 * without importing, which is the whole point. A new reference, so the caller
 * owns it: returning a borrowed one out of a PY_OWNED scope is the shape
 * owned.h warns about, even where the module would have kept it alive.
 *
 * NULL with no exception set is the absent module, which is the ordinary answer
 * and turns the guard off for this class because there is nothing it could be
 * naming. NULL *with* an exception set is a failure, and the caller has to tell
 * them apart: swallowing the second one turns a MemoryError into a silently
 * unguarded class, which is #14 again with no way to notice.
 */
static PyObject * module_attribute(char const * const module_name, char const * const attribute) {
	PY_OWNED(name, PyUnicode_FromString(module_name));

	if (name == NULL) {
		return NULL;
	}

	PY_OWNED(module, PyImport_GetModule(name));

	if (module == NULL) {
		return NULL;
	}

	PyObject * const found = PyObject_GetAttrString(module, attribute);

	/* A module that exists without the attribute is a stdlib salix does not
	 * recognise, not a failure. Anything else is. */
	if (found == NULL && PyErr_ExceptionMatches(PyExc_AttributeError)) {
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

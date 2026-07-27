#include <Python.h>
#include <stdbool.h>

#include "annotations.h"
#include "fields.h"
#include "owned.h"
#include "result.h"
#include "types.h"

static enum result append_inherited(
	RecordType const * base,
	PyObject * all_names,
	PyObject * default_by_name
);
static enum result append_declared(
	RecordType const * base,
	PyObject * annotations,
	PyObject * namespace,
	PyObject * all_names,
	PyObject * new_names,
	PyObject * default_by_name
);
static PyObject * build_defaults(PyObject * all_names, PyObject * default_by_name);
static PyObject * checked_annotations(PyObject * namespace);
static bool inherits_field(RecordType const * base, PyObject * field_name);

/* The plan only takes references once every step has succeeded; the working
 * collections belong to this scope either way. */
struct field_plan field_plan_build(RecordType const * const base, PyObject * const namespace) {
	struct field_plan plan = {0};

	PY_OWN(annotations, checked_annotations(namespace));

	if (annotations == NULL) {
		return plan;
	}

	PY_OWN(all_names, PyList_New(0));
	PY_OWN(new_names, PyList_New(0));
	PY_OWN(default_by_name, PyDict_New());

	if (all_names != NULL
		&& new_names != NULL
		&& default_by_name != NULL
		&& append_inherited(base, all_names, default_by_name) == RESULT_OK
		&& append_declared(base, annotations, namespace, all_names, new_names, default_by_name) == RESULT_OK) {
		plan.defaults = build_defaults(all_names, default_by_name);

		if (plan.defaults != NULL) {
			plan.all_names = py_steal(&all_names);
			plan.new_names = py_steal(&new_names);
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
	PY_OWN(annotations, record_annotations(namespace));

	if (annotations == NULL || PyDict_Check(annotations)) {
		return py_steal(&annotations);
	}

	PyErr_SetString(PyExc_TypeError, "__annotations__ must be a dict");

	return NULL;
}

/* Inherited fields keep their position and defaults. */
static enum result append_inherited(
	RecordType const * const base,
	PyObject * const all_names,
	PyObject * const default_by_name
) {
	if (base == NULL) {
		return RESULT_OK;
	}

	Py_ssize_t const required_count = record_required_count(base);

	for (Py_ssize_t i = 0; i < base->record_field_count; ++i) {
		PyObject * const field_name = PyTuple_GET_ITEM(base->record_field_names, i);

		if (PyList_Append(all_names, field_name) < 0) {
			return RESULT_ERROR;
		}

		if (i < required_count) {
			continue;
		}

		PyObject * const inherited_default =
			PyTuple_GET_ITEM(base->record_defaults, i - required_count);

		if (PyDict_SetItem(default_by_name, field_name, inherited_default) < 0) {
			return RESULT_ERROR;
		}
	}

	return RESULT_OK;
}

/* New fields come from this class's annotations, in declaration order. */
static enum result append_declared(
	RecordType const * const base,
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

		if (declared_default != NULL
			&& PyDict_SetItem(default_by_name, field_name, declared_default) < 0) {
			return RESULT_ERROR;
		}

		/* Skip if this name was already inherited (override of annotation, not
		 * a new slot). */
		if (inherits_field(base, field_name)) {
			continue;
		}

		if (PyList_Append(all_names, field_name) < 0 || PyList_Append(new_names, field_name) < 0) {
			return RESULT_ERROR;
		}
	}

	return RESULT_OK;
}

static bool inherits_field(RecordType const * const base, PyObject * const field_name) {
	Py_ssize_t const inherited_count = base != NULL ? base->record_field_count : 0;

	for (Py_ssize_t i = 0; i < inherited_count; ++i) {
		if (PyUnicode_Compare(field_name, PyTuple_GET_ITEM(base->record_field_names, i)) == 0) {
			return true;
		}
	}

	return false;
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
				"non-default field '%U' follows a field with a default", field_name
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

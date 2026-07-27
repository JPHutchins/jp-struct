/* record — a minimal, C-backed, inheritable ``Record`` base class.
 *
 * This is the seed implementation copied from the feasibility prototype
 * (JPHutchins/record-type PR #1). It answers: can record-type's features be
 * delivered as an inheritable base class (like ``typing.NamedTuple``)
 * implemented in C, with msgspec-class type-creation speed but a tiny fraction
 * of msgspec's import cost?
 *
 * The design is a deliberately stripped-down clone of msgspec's ``Struct``
 * machinery (see msgspec's src/msgspec/_core.c):
 *
 *   - ``RecordMeta``  : a C metaclass whose ``tp_new`` builds the record type
 *                       at class-creation time, entirely in C (no ``exec``, no
 *                       ``inspect``).  This is what makes type creation fast.
 *   - ``Record``      : the public base class.  Subclasses declare fields with
 *                       class-body annotations (``a: int``), exactly like
 *                       NamedTuple/dataclass/msgspec.
 *   - per-type vectorcall for instance creation (fields written straight to
 *     slot memory, no Python bytecode), a frozen ``tp_setattro``, and C-level
 *     ``__eq__`` / ``__hash__`` / ``__repr__`` inherited from a mixin base.
 *
 * What this intentionally does NOT do (so the .so stays tiny and the import
 * stays sub-millisecond): no serialization, no validation, no type coercion,
 * no JSON/msgpack — none of msgspec's ~22k lines of codec. Class-body syntax
 * also cannot express positional-only / keyword-only / *args / **kwargs.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "meta.h"
#include "mixin.h"
#include "result.h"

static int record_exec(PyObject * module);
static enum result add_record_base(PyObject * module);
static PyObject * create_record_base(void);

static PyModuleDef_Slot record_slots[] = {
	{Py_mod_exec, record_exec},
#ifdef Py_mod_multiple_interpreters
	{Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
#endif
	{0, NULL},
};

static PyModuleDef record_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "record",
	.m_doc = "A minimal C-backed inheritable Record base class.",
	.m_size = 0,
	.m_slots = record_slots,
};

PyMODINIT_FUNC PyInit_record(void) {
	return PyModuleDef_Init(&record_module);
}

static int record_exec(PyObject * const module) {
	RecordMeta_Type.tp_base = &PyType_Type;

	if (PyType_Ready(&RecordMeta_Type) < 0 || PyType_Ready(&RecordMixin_Type) < 0) {
		return RESULT_ERROR;
	}

	if (add_record_base(module) != RESULT_OK) {
		return RESULT_ERROR;
	}

	return PyModule_AddObjectRef(module, "RecordMeta", (PyObject *) &RecordMeta_Type);
}

static enum result add_record_base(PyObject * const module) {
	PyObject * const record_base = create_record_base();

	if (record_base == NULL) {
		return RESULT_ERROR;
	}

	int const added = PyModule_AddObjectRef(module, "Record", record_base);
	Py_DECREF(record_base);

	return added < 0 ? RESULT_ERROR : RESULT_OK;
}

/* Build the public ``Record`` base via the metaclass, so its metaclass is
 * RecordMeta and it inherits the dunders from the mixin. */
static PyObject * create_record_base(void) {
	PyObject * const name = PyUnicode_FromString("Record");
	PyObject * const bases = PyTuple_Pack(1, (PyObject *) &RecordMixin_Type);
	PyObject * const namespace = PyDict_New();
	PyObject * const args = name != NULL && bases != NULL && namespace != NULL
		? PyTuple_Pack(3, name, bases, namespace)
		: NULL;

	Py_XDECREF(name);
	Py_XDECREF(bases);
	Py_XDECREF(namespace);

	if (args == NULL) {
		return NULL;
	}

	PyObject * const record_base = RecordMeta_new(&RecordMeta_Type, args, NULL);
	Py_DECREF(args);

	return record_base;
}

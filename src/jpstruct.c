/* struct — a minimal, C-backed, inheritable ``Struct`` base class.
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
 *   - ``StructMeta``  : a C metaclass whose ``tp_new`` builds the struct type
 *                       at class-creation time, entirely in C (no ``exec``, no
 *                       ``inspect``).  This is what makes type creation fast.
 *   - ``Struct``      : the public base class.  Subclasses declare fields with
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
#include "owned.h"
#include "result.h"

static int struct_exec(PyObject * module);
static enum result add_struct_base(PyObject * module);
static PyObject * create_struct_base(void);

static PyModuleDef_Slot struct_slots[] = {
	{Py_mod_exec, struct_exec},
#ifdef Py_mod_multiple_interpreters
	{Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
#endif
	{0, NULL},
};

static PyModuleDef struct_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "jpstruct",
	.m_doc = "A minimal C-backed inheritable Struct base class.",
	.m_size = 0,
	.m_slots = struct_slots,
};

PyMODINIT_FUNC PyInit_jpstruct(void) {
	return PyModuleDef_Init(&struct_module);
}

static int struct_exec(PyObject * const module) {
	StructMeta_Type.tp_base = &PyType_Type;

	if (PyType_Ready(&StructMeta_Type) < 0 || PyType_Ready(&StructMixin_Type) < 0) {
		return RESULT_ERROR;
	}

	if (add_struct_base(module) != RESULT_OK) {
		return RESULT_ERROR;
	}

	return PyModule_AddObjectRef(module, "StructMeta", (PyObject *) &StructMeta_Type);
}

static enum result add_struct_base(PyObject * const module) {
	PyObject * const struct_base = create_struct_base();

	if (struct_base == NULL) {
		return RESULT_ERROR;
	}

	int const added = PyModule_AddObjectRef(module, "Struct", struct_base);
	Py_DECREF(struct_base);

	return added < 0 ? RESULT_ERROR : RESULT_OK;
}

/* Build the public ``Struct`` base via the metaclass, so its metaclass is
 * StructMeta and it inherits the dunders from the mixin. */
static PyObject * create_struct_base(void) {
	PY_OWNED(name, PyUnicode_FromString("Struct"));
	PY_OWNED(module_name, PyUnicode_FromString(struct_module.m_name));
	PY_OWNED(bases, PyTuple_Pack(1, (PyObject *) &StructMixin_Type));
	PY_OWNED(namespace, PyDict_New());

	if (name == NULL || module_name == NULL || bases == NULL || namespace == NULL) {
		return NULL;
	}

	/* A class body supplies __module__; an empty namespace leaves the type to
	 * take it from whatever frame the import machinery is running. */
	if (PyDict_SetItemString(namespace, "__module__", module_name) < 0) {
		return NULL;
	}

	PY_OWNED(args, PyTuple_Pack(3, name, bases, namespace));

	if (args == NULL) {
		return NULL;
	}

	return StructMeta_new(&StructMeta_Type, args, NULL);
}

/* salix — a minimal, C-backed, inheritable ``Struct`` base class.
 *
 * It answers a question first asked of record-type: can those features be
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
 *     slot memory, no Python bytecode), and C-level ``__eq__`` / ``__hash__`` /
 *     ``__repr__`` / ``__setattr__`` from a mixin base -- the last of which is
 *     what makes a frozen class frozen.
 *
 * What this intentionally does NOT do (so the .so stays tiny and the import
 * stays sub-millisecond): no serialization, no validation, no type coercion,
 * no JSON/msgpack — none of msgspec's ~22k lines of codec. Class-body syntax
 * also cannot express positional-only / keyword-only / *args / **kwargs.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "construct.h"
#include "meta.h"
#include "mixin.h"
#include "owned.h"
#include "result.h"

static int struct_exec(PyObject * module);
static enum result add_struct_base(PyObject * module);
static PyObject * create_struct_base(void);

/*
 * A type's field metadata is written once at class creation and then only
 * read. An instance's slots are written by the constructor before it returns,
 * and afterwards only through PyMember_SetOne, so a free-threaded build's
 * guarantees there are inherited rather than reimplemented.
 */
static PyModuleDef_Slot struct_slots[] = {
	{Py_mod_exec, struct_exec},
#ifdef Py_mod_multiple_interpreters
	{Py_mod_multiple_interpreters, Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED},
#endif
#ifdef Py_mod_gil
	{Py_mod_gil, Py_MOD_GIL_NOT_USED},
#endif
	{0, NULL},
};

static PyMethodDef struct_functions[] = {
	{
		.ml_name = "set_field",
		.ml_meth = Struct_set_field,
		.ml_flags = METH_VARARGS,
		.ml_doc = "set_field(instance, name, value) -- assign a field the class declared.",
	},
	{.ml_name = NULL},
};

static PyModuleDef struct_module = {
	PyModuleDef_HEAD_INIT,
	.m_name = "salix",
	.m_doc = "A minimal C-backed inheritable Struct base class.",
	.m_size = 0,
	.m_slots = struct_slots,
	.m_methods = struct_functions,
};

PyMODINIT_FUNC PyInit_salix(void) {
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

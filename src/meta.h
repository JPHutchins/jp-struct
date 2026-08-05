#pragma once

#include <Python.h>

/* The metaclass: an instance of it *is* a struct class.  Not const, for the
 * same reason StructMixin_Type is not. */
extern PyTypeObject StructMeta_Type;

PyObject * StructMeta_new(PyTypeObject * metatype, PyObject * args, PyObject * keywords);

/* The public ``Struct`` base, built once at module init. It is the one class
 * with no struct base among its own, which is exactly what StructMeta_new
 * refuses -- so it is built here instead of through a metaclass call. */
PyObject * struct_create_root(PyObject * name, PyObject * bases, PyObject * namespace);

#pragma once

#include <Python.h>

/* The metaclass: an instance of it *is* a struct class.  Not const, for the
 * same reason StructMixin_Type is not. */
extern PyTypeObject StructMeta_Type;

/* Also called directly at module init to build the public ``Struct`` base. */
PyObject * StructMeta_new(PyTypeObject * metatype, PyObject * args, PyObject * keywords);

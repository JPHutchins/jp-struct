#pragma once

#include <Python.h>

/* The metaclass: an instance of it *is* a record class.  Not const, for the
 * same reason RecordMixin_Type is not. */
extern PyTypeObject RecordMeta_Type;

/* Also called directly at module init to build the public ``Record`` base. */
PyObject * RecordMeta_new(PyTypeObject * metatype, PyObject * args, PyObject * keywords);

#pragma once

#include <Python.h>

/* The base every record inherits the dunders from.  It holds no fields of its
 * own, so `isinstance(x, Record)` is the same question as "is this a record".
 *
 * Not const: PyType_Ready fills in tp_dict, tp_mro, tp_bases and the inherited
 * slots at import time, and PyObject_TypeCheck takes a mutable pointer. */
extern PyTypeObject RecordMixin_Type;

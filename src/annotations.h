#pragma once

#include <Python.h>

/* Return a *new* reference to the ordered annotations dict for a class
 * namespace.  On <3.14 the namespace carries ``__annotations__`` directly; on
 * 3.14+ (PEP 649) only ``__annotate_func__`` is present and must be called.
 * Returns a new (possibly empty) dict, or NULL with an exception set. */
PyObject * struct_annotations(PyObject * namespace);

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
#include <structmember.h>

#ifndef Py_TPFLAGS_HAVE_VECTORCALL
#define Py_TPFLAGS_HAVE_VECTORCALL _Py_TPFLAGS_HAVE_VECTORCALL
#endif

/* Access the PyMemberDef array that floats behind a heap type. Mirrors
 * msgspec's MS_PyHeapType_GET_MEMBERS: the members live just past the type
 * object, which (for a custom metaclass) is sized by the metaclass basicsize. */
#define RECORD_HEAPTYPE_MEMBERS(etype) \
    ((PyMemberDef *)(((char *)(etype)) + Py_TYPE(etype)->tp_basicsize))

/* An instance of RecordMeta *is* a record class.  We extend the heap-type
 * object with the per-type field metadata needed for fast construction and
 * the dunder methods. */
typedef struct {
    PyHeapTypeObject ht;
    PyObject *record_fields;    /* tuple[str]: every field name, in order   */
    PyObject *record_defaults;  /* tuple: defaults for the trailing fields  */
    Py_ssize_t *record_offsets; /* malloc'd array[nfields] of slot offsets  */
    Py_ssize_t record_nfields;
    Py_ssize_t record_ndefaults;
} RecordType;

static PyTypeObject RecordMeta_Type;
static PyTypeObject RecordMixin_Type;

static PyObject *
Record_vectorcall(PyObject *cls_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames);

/* ----------------------------------------------------------------------- *
 * Annotation extraction (cross-version, PEP 649 aware)                    *
 * ----------------------------------------------------------------------- */

/* Return a *new* reference to the ordered annotations dict for a class
 * namespace.  On <3.14 the namespace carries ``__annotations__`` directly; on
 * 3.14+ (PEP 649) only ``__annotate_func__`` is present and must be called.
 * Returns a new (possibly empty) dict, or NULL with an exception set. */
static PyObject *
record_get_annotations(PyObject *namespace)
{
    PyObject *ann = PyDict_GetItemString(namespace, "__annotations__");
    if (ann != NULL) {
        Py_INCREF(ann);
        return ann;
    }

    PyObject *annotate = PyDict_GetItemString(namespace, "__annotate__");
    if (annotate == NULL) {
        annotate = PyDict_GetItemString(namespace, "__annotate_func__");
    }
    if (annotate == NULL) {
        return PyDict_New();
    }

    /* annotationlib.Format.FORWARDREF == 2: never raises NameError on an
     * unresolved bare forward reference, which is all we need since we only
     * read the field *names* and *order* here. */
    PyObject *fmt = PyLong_FromLong(2);
    if (fmt == NULL) {
        return NULL;
    }
    PyObject *out = PyObject_CallOneArg(annotate, fmt);
    Py_DECREF(fmt);
    if (out == NULL) {
        /* Fall back to VALUE format if FORWARDREF is unsupported. */
        PyErr_Clear();
        fmt = PyLong_FromLong(1);
        if (fmt == NULL) {
            return NULL;
        }
        out = PyObject_CallOneArg(annotate, fmt);
        Py_DECREF(fmt);
    }
    return out;
}

/* ----------------------------------------------------------------------- *
 * Type creation: RecordMeta.__new__                                       *
 * ----------------------------------------------------------------------- */

/* Find the (single) record base among ``bases``, or NULL if none has fields. */
static RecordType *
record_find_base(PyObject *bases)
{
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); i++) {
        PyObject *base = PyTuple_GET_ITEM(bases, i);
        if (PyObject_TypeCheck(base, &RecordMeta_Type)) {
            RecordType *rt = (RecordType *)base;
            if (rt->record_nfields > 0) {
                return rt;
            }
        }
    }
    return NULL;
}

static PyObject *
RecordMeta_new(PyTypeObject *metatype, PyObject *args, PyObject *kwds)
{
    (void)kwds;  /* class keywords (frozen=, kw_only=, ...) not yet supported */
    PyObject *name, *bases, *orig_ns;
    if (!PyArg_ParseTuple(
            args, "UO!O!:RecordMeta.__new__",
            &name, &PyTuple_Type, &bases, &PyDict_Type, &orig_ns)) {
        return NULL;
    }

    PyObject *annotations = NULL, *new_ns = NULL, *new_slots = NULL;
    PyObject *field_names = NULL, *defaults = NULL, *match_args = NULL;
    PyObject *fields_tuple = NULL;
    Py_ssize_t *offsets = NULL;
    RecordType *cls = NULL;
    PyObject *result = NULL;

    RecordType *base = record_find_base(bases);
    Py_ssize_t n_inherited = (base == NULL) ? 0 : base->record_nfields;

    field_names = PyList_New(0);         /* every field name, in order */
    if (field_names == NULL) goto cleanup;
    PyObject *default_map = PyDict_New(); /* name -> default object */
    if (default_map == NULL) goto cleanup;

    /* Inherited fields keep their position and defaults. */
    for (Py_ssize_t i = 0; i < n_inherited; i++) {
        PyObject *fname = PyTuple_GET_ITEM(base->record_fields, i);
        if (PyList_Append(field_names, fname) < 0) { Py_DECREF(default_map); goto cleanup; }
        Py_ssize_t base_npos = base->record_nfields - base->record_ndefaults;
        if (i >= base_npos) {
            PyObject *d = PyTuple_GET_ITEM(base->record_defaults, i - base_npos);
            if (PyDict_SetItem(default_map, fname, d) < 0) { Py_DECREF(default_map); goto cleanup; }
        }
    }

    /* New fields come from this class's annotations, in declaration order. */
    annotations = record_get_annotations(orig_ns);
    if (annotations == NULL) { Py_DECREF(default_map); goto cleanup; }
    if (!PyDict_Check(annotations)) {
        PyErr_SetString(PyExc_TypeError, "__annotations__ must be a dict");
        Py_DECREF(default_map);
        goto cleanup;
    }

    new_slots = PyList_New(0);   /* only the names this class adds */
    if (new_slots == NULL) { Py_DECREF(default_map); goto cleanup; }

    PyObject *ann_key, *ann_val;
    Py_ssize_t ann_pos = 0;
    while (PyDict_Next(annotations, &ann_pos, &ann_key, &ann_val)) {
        if (!PyUnicode_CheckExact(ann_key)) {
            PyErr_SetString(PyExc_TypeError, "annotation keys must be strings");
            Py_DECREF(default_map);
            goto cleanup;
        }

        /* Skip if this name was already inherited (override of annotation, not
         * a new slot). */
        int inherited = 0;
        for (Py_ssize_t i = 0; i < n_inherited; i++) {
            if (PyUnicode_Compare(ann_key, PyTuple_GET_ITEM(base->record_fields, i)) == 0) {
                inherited = 1;
                break;
            }
        }

        /* A default is the class-body value bound to the field name. */
        PyObject *dflt = PyDict_GetItem(orig_ns, ann_key);  /* borrowed */
        if (dflt != NULL) {
            if (PyDict_SetItem(default_map, ann_key, dflt) < 0) { Py_DECREF(default_map); goto cleanup; }
        }

        if (!inherited) {
            if (PyList_Append(field_names, ann_key) < 0) { Py_DECREF(default_map); goto cleanup; }
            if (PyList_Append(new_slots, ann_key) < 0) { Py_DECREF(default_map); goto cleanup; }
        }
    }

    Py_ssize_t nfields = PyList_GET_SIZE(field_names);

    /* Build the defaults tuple as the trailing run of defaulted fields, and
     * enforce that no required field follows a defaulted one (same rule as
     * Python function signatures). */
    Py_ssize_t first_default = nfields;
    for (Py_ssize_t i = 0; i < nfields; i++) {
        PyObject *fname = PyList_GET_ITEM(field_names, i);
        int has = PyDict_Contains(default_map, fname);
        if (has < 0) { Py_DECREF(default_map); goto cleanup; }
        if (has) {
            if (first_default == nfields) first_default = i;
        }
        else if (first_default != nfields) {
            PyErr_Format(
                PyExc_TypeError,
                "non-default field '%U' follows a field with a default", fname);
            Py_DECREF(default_map);
            goto cleanup;
        }
    }
    Py_ssize_t ndefaults = nfields - first_default;
    defaults = PyTuple_New(ndefaults);
    if (defaults == NULL) { Py_DECREF(default_map); goto cleanup; }
    for (Py_ssize_t i = 0; i < ndefaults; i++) {
        PyObject *fname = PyList_GET_ITEM(field_names, first_default + i);
        PyObject *d = PyDict_GetItem(default_map, fname);  /* borrowed */
        Py_INCREF(d);
        PyTuple_SET_ITEM(defaults, i, d);
    }
    Py_DECREF(default_map);

    fields_tuple = PyList_AsTuple(field_names);
    if (fields_tuple == NULL) goto cleanup;
    match_args = PyList_AsTuple(field_names);
    if (match_args == NULL) goto cleanup;

    /* Build the namespace handed to type.__new__: a copy of the original with
     * the default-bearing field names removed (so __slots__ won't clash with a
     * class variable) plus __slots__ / __match_args__. */
    new_ns = PyDict_Copy(orig_ns);
    if (new_ns == NULL) goto cleanup;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(new_slots); i++) {
        PyObject *fname = PyList_GET_ITEM(new_slots, i);
        if (PyDict_Contains(new_ns, fname) == 1) {
            if (PyDict_DelItem(new_ns, fname) < 0) goto cleanup;
        }
    }
    PyObject *slots_tuple = PyList_AsTuple(new_slots);
    if (slots_tuple == NULL) goto cleanup;
    int rc = PyDict_SetItemString(new_ns, "__slots__", slots_tuple);
    Py_DECREF(slots_tuple);
    if (rc < 0) goto cleanup;
    if (PyDict_SetItemString(new_ns, "__match_args__", match_args) < 0) goto cleanup;

    /* Create the type. */
    PyObject *type_args = Py_BuildValue("(OOO)", name, bases, new_ns);
    if (type_args == NULL) goto cleanup;
    cls = (RecordType *)PyType_Type.tp_new(metatype, type_args, NULL);
    Py_DECREF(type_args);
    if (cls == NULL) goto cleanup;

    /* Map each NEW slot name to its byte offset via the type's member defs. */
    offsets = PyMem_New(Py_ssize_t, nfields > 0 ? nfields : 1);
    if (offsets == NULL) { PyErr_NoMemory(); goto cleanup; }
    for (Py_ssize_t i = 0; i < n_inherited; i++) {
        offsets[i] = base->record_offsets[i];
    }
    {
        PyMemberDef *mp = RECORD_HEAPTYPE_MEMBERS(cls);
        Py_ssize_t nmembers = Py_SIZE(cls);
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(new_slots); i++) {
            PyObject *fname = PyList_GET_ITEM(new_slots, i);
            Py_ssize_t off = -1;
            for (Py_ssize_t j = 0; j < nmembers; j++) {
                if (PyUnicode_CompareWithASCIIString(fname, mp[j].name) == 0) {
                    off = mp[j].offset;
                    break;
                }
            }
            if (off < 0) {
                PyErr_Format(PyExc_RuntimeError, "could not find slot offset for %R", fname);
                goto cleanup;
            }
            offsets[n_inherited + i] = off;
        }
    }

    cls->record_fields = fields_tuple;   fields_tuple = NULL;  /* stolen */
    cls->record_defaults = defaults;     defaults = NULL;      /* stolen */
    cls->record_offsets = offsets;       offsets = NULL;       /* stolen */
    cls->record_nfields = nfields;
    cls->record_ndefaults = ndefaults;

    ((PyTypeObject *)cls)->tp_vectorcall = (vectorcallfunc)Record_vectorcall;

    result = (PyObject *)cls;
    cls = NULL;

cleanup:
    Py_XDECREF(annotations);
    Py_XDECREF(field_names);
    Py_XDECREF(new_slots);
    Py_XDECREF(new_ns);
    Py_XDECREF(match_args);
    Py_XDECREF(fields_tuple);
    Py_XDECREF(defaults);
    if (offsets != NULL) PyMem_Free(offsets);
    Py_XDECREF((PyObject *)cls);
    return result;
}

/* ----------------------------------------------------------------------- *
 * RecordMeta GC + dealloc (it owns fields/defaults tuples + offsets array) *
 * ----------------------------------------------------------------------- */

static int
RecordMeta_traverse(PyObject *self, visitproc visit, void *arg)
{
    RecordType *rt = (RecordType *)self;
    Py_VISIT(rt->record_fields);
    Py_VISIT(rt->record_defaults);
    return PyType_Type.tp_traverse(self, visit, arg);
}

static int
RecordMeta_clear(PyObject *self)
{
    RecordType *rt = (RecordType *)self;
    if (rt->record_fields == NULL) return 0;  /* already cleared */
    Py_CLEAR(rt->record_fields);
    Py_CLEAR(rt->record_defaults);
    if (rt->record_offsets != NULL) {
        PyMem_Free(rt->record_offsets);
        rt->record_offsets = NULL;
    }
    return PyType_Type.tp_clear(self);
}

static void
RecordMeta_dealloc(PyObject *self)
{
    /* GC invariants require dealloc to untrack immediately, but
     * PyType_Type.tp_dealloc assumes the type is currently tracked — hence the
     * untrack / clear / re-track dance (mirrors msgspec's StructMeta_dealloc). */
    PyObject_GC_UnTrack(self);
    RecordMeta_clear(self);
    PyObject_GC_Track(self);
    PyType_Type.tp_dealloc(self);
}

/* ----------------------------------------------------------------------- *
 * Instance creation: per-type vectorcall                                  *
 * ----------------------------------------------------------------------- */

static PyObject *
Record_vectorcall(PyObject *cls_obj, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    RecordType *rt = (RecordType *)cls_obj;
    PyTypeObject *cls = (PyTypeObject *)cls_obj;
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    Py_ssize_t nkw = (kwnames == NULL) ? 0 : PyTuple_GET_SIZE(kwnames);
    Py_ssize_t nfields = rt->record_nfields;
    Py_ssize_t npos = nfields - rt->record_ndefaults;

    if (nargs > nfields) {
        PyErr_Format(
            PyExc_TypeError,
            "%.200s() takes at most %zd positional arguments but %zd were given",
            cls->tp_name, nfields, nargs);
        return NULL;
    }

    PyObject *self = cls->tp_alloc(cls, 0);
    if (self == NULL) return NULL;

    for (Py_ssize_t i = 0; i < nargs; i++) {
        PyObject *v = args[i];
        Py_INCREF(v);
        *(PyObject **)((char *)self + rt->record_offsets[i]) = v;
    }

    for (Py_ssize_t i = 0; i < nkw; i++) {
        PyObject *kwname = PyTuple_GET_ITEM(kwnames, i);
        PyObject *val = args[nargs + i];
        Py_ssize_t fi;
        for (fi = 0; fi < nfields; fi++) {
            PyObject *field = PyTuple_GET_ITEM(rt->record_fields, fi);
            if (kwname == field) goto found;
        }
        for (fi = 0; fi < nfields; fi++) {
            PyObject *field = PyTuple_GET_ITEM(rt->record_fields, fi);
            int eq = PyUnicode_Compare(kwname, field);
            if (eq == 0) goto found;
            if (eq == -1 && PyErr_Occurred()) goto error;
        }
        PyErr_Format(
            PyExc_TypeError, "%.200s() got an unexpected keyword argument '%U'",
            cls->tp_name, kwname);
        goto error;
    found:
        {
            PyObject **addr = (PyObject **)((char *)self + rt->record_offsets[fi]);
            if (*addr != NULL || fi < nargs) {
                PyErr_Format(
                    PyExc_TypeError, "%.200s() got multiple values for argument '%U'",
                    cls->tp_name, kwname);
                goto error;
            }
            Py_INCREF(val);
            *addr = val;
        }
    }

    for (Py_ssize_t fi = nargs; fi < nfields; fi++) {
        PyObject **addr = (PyObject **)((char *)self + rt->record_offsets[fi]);
        if (*addr != NULL) continue;
        if (fi >= npos) {
            PyObject *d = PyTuple_GET_ITEM(rt->record_defaults, fi - npos);
            Py_INCREF(d);
            *addr = d;
        }
        else {
            PyErr_Format(
                PyExc_TypeError, "%.200s() missing required argument '%U'",
                cls->tp_name, PyTuple_GET_ITEM(rt->record_fields, fi));
            goto error;
        }
    }

    return self;

error:
    Py_DECREF(self);
    return NULL;
}

/* ----------------------------------------------------------------------- *
 * Immutability + dunders (defined on the mixin, inherited by all records) *
 * ----------------------------------------------------------------------- */

static int
Record_setattro(PyObject *self, PyObject *name, PyObject *value)
{
    (void)name;
    if (value == NULL) {
        PyErr_Format(
            PyExc_TypeError,
            "%.200s object does not support attribute deletion",
            Py_TYPE(self)->tp_name);
    }
    else {
        PyErr_Format(
            PyExc_TypeError,
            "%.200s object does not support attribute assignment",
            Py_TYPE(self)->tp_name);
    }
    return -1;
}

static PyObject *
Record_repr(PyObject *self)
{
    RecordType *rt = (RecordType *)Py_TYPE(self);
    Py_ssize_t nfields = rt->record_nfields;

    int recursive = Py_ReprEnter(self);
    if (recursive != 0) {
        return (recursive < 0) ? NULL : PyUnicode_FromString("...");
    }

    PyObject *parts = PyList_New(nfields);
    if (parts == NULL) { Py_ReprLeave(self); return NULL; }
    for (Py_ssize_t i = 0; i < nfields; i++) {
        PyObject *field = PyTuple_GET_ITEM(rt->record_fields, i);
        PyObject *val = *(PyObject **)((char *)self + rt->record_offsets[i]);
        PyObject *vr = (val == NULL)
            ? PyUnicode_FromString("<unset>")
            : PyObject_Repr(val);
        if (vr == NULL) { Py_DECREF(parts); Py_ReprLeave(self); return NULL; }
        PyObject *piece = PyUnicode_FromFormat("%U=%U", field, vr);
        Py_DECREF(vr);
        if (piece == NULL) { Py_DECREF(parts); Py_ReprLeave(self); return NULL; }
        PyList_SET_ITEM(parts, i, piece);
    }

    PyObject *sep = PyUnicode_FromString(", ");
    PyObject *inner = sep ? PyUnicode_Join(sep, parts) : NULL;
    Py_XDECREF(sep);
    Py_DECREF(parts);
    Py_ReprLeave(self);
    if (inner == NULL) return NULL;

    PyObject *out = PyUnicode_FromFormat("%s(%U)", Py_TYPE(self)->tp_name, inner);
    Py_DECREF(inner);
    return out;
}

static Py_hash_t
Record_hash(PyObject *self)
{
    RecordType *rt = (RecordType *)Py_TYPE(self);
    Py_ssize_t nfields = rt->record_nfields;
    PyObject *t = PyTuple_New(nfields);
    if (t == NULL) return -1;
    for (Py_ssize_t i = 0; i < nfields; i++) {
        PyObject *val = *(PyObject **)((char *)self + rt->record_offsets[i]);
        if (val == NULL) val = Py_None;
        Py_INCREF(val);
        PyTuple_SET_ITEM(t, i, val);
    }
    Py_hash_t h = PyObject_Hash(t);
    Py_DECREF(t);
    return h;
}

static PyObject *
Record_richcompare(PyObject *self, PyObject *other, int op)
{
    if (op != Py_EQ && op != Py_NE) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    if (!PyObject_TypeCheck(other, &RecordMixin_Type)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    RecordType *a = (RecordType *)Py_TYPE(self);
    RecordType *b = (RecordType *)Py_TYPE(other);

    /* Structural: equal iff the field-name tuples match and every value
     * compares equal.  Nominal type identity is deliberately not required. */
    int names_eq = PyObject_RichCompareBool(a->record_fields, b->record_fields, Py_EQ);
    if (names_eq < 0) return NULL;
    int equal = names_eq;
    if (equal) {
        for (Py_ssize_t i = 0; i < a->record_nfields; i++) {
            PyObject *va = *(PyObject **)((char *)self + a->record_offsets[i]);
            PyObject *vb = *(PyObject **)((char *)other + b->record_offsets[i]);
            if (va == NULL) va = Py_None;  /* guard unset slots, like hash/repr */
            if (vb == NULL) vb = Py_None;
            int cmp = PyObject_RichCompareBool(va, vb, Py_EQ);
            if (cmp < 0) return NULL;
            if (!cmp) { equal = 0; break; }
        }
    }
    if (op == Py_NE) equal = !equal;
    if (equal) Py_RETURN_TRUE;
    Py_RETURN_FALSE;
}

/* ----------------------------------------------------------------------- *
 * Introspection getsets on the mixin                                      *
 * ----------------------------------------------------------------------- */

static PyObject *
Record_get_fields(PyObject *self, void *closure)
{
    (void)closure;
    PyObject *out = ((RecordType *)Py_TYPE(self))->record_fields;
    Py_XINCREF(out);
    return out ? out : PyTuple_New(0);
}

static PyObject *
Record_get_defaults(PyObject *self, void *closure)
{
    (void)closure;
    PyObject *out = ((RecordType *)Py_TYPE(self))->record_defaults;
    Py_XINCREF(out);
    return out ? out : PyTuple_New(0);
}

static PyGetSetDef Record_getset[] = {
    {"__record_fields__", Record_get_fields, NULL, "tuple of field names", NULL},
    {"__record_defaults__", Record_get_defaults, NULL, "tuple of trailing defaults", NULL},
    {NULL},
};

/* ----------------------------------------------------------------------- *
 * Static type objects                                                     *
 * ----------------------------------------------------------------------- */

static PyTypeObject RecordMeta_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "record.RecordMeta",
    .tp_basicsize = sizeof(RecordType),
    .tp_itemsize = sizeof(PyMemberDef),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_TYPE_SUBCLASS
              | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_VECTORCALL
              | Py_TPFLAGS_BASETYPE,
    .tp_new = RecordMeta_new,
    .tp_dealloc = RecordMeta_dealloc,
    .tp_traverse = RecordMeta_traverse,
    .tp_clear = RecordMeta_clear,
    .tp_call = PyVectorcall_Call,
    .tp_vectorcall_offset = offsetof(PyTypeObject, tp_vectorcall),
};

static PyTypeObject RecordMixin_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "record._RecordMixin",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_setattro = Record_setattro,
    .tp_repr = Record_repr,
    .tp_hash = Record_hash,
    .tp_richcompare = Record_richcompare,
    .tp_getset = Record_getset,
};

/* ----------------------------------------------------------------------- *
 * Module init                                                             *
 * ----------------------------------------------------------------------- */

static int
record_exec(PyObject *module)
{
    RecordMeta_Type.tp_base = &PyType_Type;
    if (PyType_Ready(&RecordMeta_Type) < 0) return -1;
    if (PyType_Ready(&RecordMixin_Type) < 0) return -1;

    /* Build the public ``Record`` base via the metaclass, so its metaclass is
     * RecordMeta and it inherits the dunders from the mixin. */
    PyObject *bases = PyTuple_Pack(1, (PyObject *)&RecordMixin_Type);
    if (bases == NULL) return -1;
    PyObject *ns = PyDict_New();
    if (ns == NULL) { Py_DECREF(bases); return -1; }
    PyObject *qualname = PyUnicode_FromString("Record");
    if (qualname == NULL) { Py_DECREF(bases); Py_DECREF(ns); return -1; }
    PyObject *args = PyTuple_Pack(3, qualname, bases, ns);
    Py_DECREF(bases);
    Py_DECREF(ns);
    Py_DECREF(qualname);
    if (args == NULL) return -1;

    PyObject *Record = RecordMeta_new(&RecordMeta_Type, args, NULL);
    Py_DECREF(args);
    if (Record == NULL) return -1;

    if (PyModule_AddObjectRef(module, "Record", Record) < 0) {
        Py_DECREF(Record);
        return -1;
    }
    Py_DECREF(Record);

    if (PyModule_AddObjectRef(module, "RecordMeta", (PyObject *)&RecordMeta_Type) < 0) {
        return -1;
    }
    return 0;
}

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

PyMODINIT_FUNC
PyInit_record(void)
{
    return PyModuleDef_Init(&record_module);
}

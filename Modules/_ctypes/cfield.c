#ifndef Py_BUILD_CORE_BUILTIN
#  define Py_BUILD_CORE_MODULE 1
#endif

#include "Python.h"
// windows.h must be included before pycore internal headers
#ifdef MS_WIN32
#  include <windows.h>
#endif

#include "pycore_bitutils.h"      // _Py_bswap32()
#include "pycore_call.h"          // _PyObject_CallNoArgs()

#include <ffi.h>
#include "ctypes.h"

#if defined(Py_HAVE_C_COMPLEX) && defined(Py_FFI_SUPPORT_C_COMPLEX)
#  include "../_complex.h"        // complex
#endif

#define CTYPES_CFIELD_CAPSULE_NAME_PYMEM "_ctypes/cfield.c pymem"

/*[clinic input]
module _ctypes
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=476a19c49b31a75c]*/

#include "clinic/cfield.c.h"

static void pymem_destructor(PyObject *ptr)
{
    void *p = PyCapsule_GetPointer(ptr, CTYPES_CFIELD_CAPSULE_NAME_PYMEM);
    if (p) {
        PyMem_Free(p);
    }
}


/******************************************************************/
/*
  PyCField_Type
*/
/*[clinic input]
class _ctypes.CField "PyObject *" "PyObject"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=602817ea3ffc709c]*/

static inline
Py_ssize_t NUM_BITS(Py_ssize_t bitsize);
static inline
Py_ssize_t LOW_BIT(Py_ssize_t offset);


/*[clinic input]
@classmethod
_ctypes.CField.__new__ as PyCField_new

    name: object(subclass_of='&PyUnicode_Type')
    type as proto: object
    size: Py_ssize_t
    offset: Py_ssize_t
    index: Py_ssize_t
    bit_size as bit_size_obj: object = None

[clinic start generated code]*/

static PyObject *
PyCField_new_impl(PyTypeObject *type, PyObject *name, PyObject *proto,
                  Py_ssize_t size, Py_ssize_t offset, Py_ssize_t index,
                  PyObject *bit_size_obj)
/*[clinic end generated code: output=43649ef9157c5f58 input=3d813f56373c4caa]*/
{
    CFieldObject* self = NULL;
    if (size < 0) {
        PyErr_Format(PyExc_ValueError,
                     "size of field %R must not be negative, got %zd",
                     name, size);
        goto error;
    }
    // assert: no overflow;
    if ((unsigned long long int) size
            >= (1ULL << (8*sizeof(Py_ssize_t)-1)) / 8) {
        PyErr_Format(PyExc_ValueError,
                     "size of field %R is too big: %zd", name, size);
        goto error;
    }

    PyTypeObject *tp = type;
    ctypes_state *st = get_module_state_by_class(tp);
    self = (CFieldObject *)tp->tp_alloc(tp, 0);
    if (!self) {
        return NULL;
    }
    if (PyUnicode_CheckExact(name)) {
        self->name = Py_NewRef(name);
    } else {
        self->name = PyObject_Str(name);
        if (!self->name) {
            goto error;
        }
    }

    StgInfo *info;
    if (PyStgInfo_FromType(st, proto, &info) < 0) {
        goto error;
    }
    if (info == NULL) {
        PyErr_Format(PyExc_TypeError,
                     "type of field %R must be a C type", self->name);
        goto error;
    }

    if (bit_size_obj != Py_None) {

        Py_ssize_t bit_size;

        if (PyLong_Check(bit_size_obj)) {
            bit_size = PyLong_AsSsize_t(bit_size_obj);
        } else {
            PyErr_Format(
                PyExc_ValueError,
                "bit size of field %R must be an integer size for bit fields",
                self->name
            );
            goto error;
        }
        assert(bit_size > 0);
        assert(bit_size <= info->size * 8);
        switch(info->ffi_type_pointer.type) {
            case FFI_TYPE_UINT8:
            case FFI_TYPE_UINT16:
            case FFI_TYPE_UINT32:
            case FFI_TYPE_SINT64:
            case FFI_TYPE_UINT64:
                break;

            case FFI_TYPE_SINT8:
            case FFI_TYPE_SINT16:
            case FFI_TYPE_SINT32:
                if (info->getfunc != _ctypes_get_fielddesc("c")->getfunc
                    && info->getfunc != _ctypes_get_fielddesc("u")->getfunc)
                {
                    break;
                }
                _Py_FALLTHROUGH;  /* else fall through */
            default:
                PyErr_Format(PyExc_TypeError,
                             "bit fields not allowed for type %s",
                             ((PyTypeObject*)proto)->tp_name);
                goto error;
        }
    }

    self->proto = Py_NewRef(proto);
    self->size = size;
    self->offset = offset;

    self->index = index;

    /*  Field descriptors for 'c_char * n' are be special cased to
        return a Python string instead of an Array object instance...
    */
    self->setfunc = NULL;
    self->getfunc = NULL;
    if (PyCArrayTypeObject_Check(st, proto)) {
        StgInfo *ainfo;
        if (PyStgInfo_FromType(st, proto, &ainfo) < 0) {
            goto error;
        }

        if (ainfo && ainfo->proto) {
            StgInfo *iinfo;
            if (PyStgInfo_FromType(st, ainfo->proto, &iinfo) < 0) {
                goto error;
            }
            if (!iinfo) {
                PyErr_SetString(PyExc_TypeError,
                                "has no _stginfo_");
                goto error;
            }
            if (iinfo->getfunc == _ctypes_get_fielddesc("c")->getfunc) {
                struct fielddesc *fd = _ctypes_get_fielddesc("s");
                self->getfunc = fd->getfunc;
                self->setfunc = fd->setfunc;
            }
            if (iinfo->getfunc == _ctypes_get_fielddesc("u")->getfunc) {
                struct fielddesc *fd = _ctypes_get_fielddesc("U");
                self->getfunc = fd->getfunc;
                self->setfunc = fd->setfunc;
            }
        }
    }

    return (PyObject *)self;
error:
    Py_XDECREF(self);
    return NULL;
}


static int
PyCField_set(CFieldObject *self, PyObject *inst, PyObject *value)
{
    CDataObject *dst;
    char *ptr;
    ctypes_state *st = get_module_state_by_class(Py_TYPE(self));
    if (!CDataObject_Check(st, inst)) {
        PyErr_SetString(PyExc_TypeError,
                        "not a ctype instance");
        return -1;
    }
    dst = (CDataObject *)inst;
    ptr = dst->b_ptr + self->offset;
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "can't delete attribute");
        return -1;
    }
    return PyCData_set(st, inst, self->proto, self->setfunc, value,
                     self->index, self->size, ptr);
}

static PyObject *
PyCField_get(CFieldObject *self, PyObject *inst, PyTypeObject *type)
{
    CDataObject *src;
    if (inst == NULL) {
        return Py_NewRef(self);
    }
    ctypes_state *st = get_module_state_by_class(Py_TYPE(self));
    if (!CDataObject_Check(st, inst)) {
        PyErr_SetString(PyExc_TypeError,
                        "not a ctype instance");
        return NULL;
    }
    src = (CDataObject *)inst;
    return PyCData_get(st, self->proto, self->getfunc, inst,
                     self->index, self->size, src->b_ptr + self->offset);
}

static PyObject *
PyCField_get_offset(PyObject *self, void *data)
{
    return PyLong_FromSsize_t(((CFieldObject *)self)->offset);
}

static PyObject *
PyCField_get_size(PyObject *self, void *data)
{
    return PyLong_FromSsize_t(((CFieldObject *)self)->size);
}

static PyGetSetDef PyCField_getset[] = {
    { "offset", PyCField_get_offset, NULL, PyDoc_STR("offset in bytes of this field") },
    { "size", PyCField_get_size, NULL, PyDoc_STR("size in bytes of this field") },
    { NULL, NULL, NULL, NULL },
};

static int
PyCField_traverse(CFieldObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));
    Py_VISIT(self->proto);
    return 0;
}

static int
PyCField_clear(CFieldObject *self)
{
    Py_CLEAR(self->proto);
    return 0;
}

static void
PyCField_dealloc(PyObject *self)
{
    PyTypeObject *tp = Py_TYPE(self);
    PyObject_GC_UnTrack(self);
    CFieldObject *self_cf = (CFieldObject *)self;
    (void)PyCField_clear(self_cf);
    Py_CLEAR(self_cf->name);
    Py_TYPE(self)->tp_free(self);
    Py_DECREF(tp);
}

static PyObject *
PyCField_repr(CFieldObject *self)
{
    PyObject *result;
    Py_ssize_t bits = NUM_BITS(self->size);
    Py_ssize_t size = LOW_BIT(self->size);
    const char *name;

    name = ((PyTypeObject *)self->proto)->tp_name;

    if (bits)
        result = PyUnicode_FromFormat(
            "<Field type=%s, ofs=%zd:%zd, bits=%zd>",
            name, self->offset, size, bits);
    else
        result = PyUnicode_FromFormat(
            "<Field type=%s, ofs=%zd, size=%zd>",
            name, self->offset, size);
    return result;
}

static PyType_Slot cfield_slots[] = {
    {Py_tp_new, PyCField_new},
    {Py_tp_dealloc, PyCField_dealloc},
    {Py_tp_repr, PyCField_repr},
    {Py_tp_doc, (void *)PyDoc_STR("Structure/Union member")},
    {Py_tp_traverse, PyCField_traverse},
    {Py_tp_clear, PyCField_clear},
    {Py_tp_getset, PyCField_getset},
    {Py_tp_descr_get, PyCField_get},
    {Py_tp_descr_set, PyCField_set},
    {0, NULL},
};

PyType_Spec cfield_spec = {
    .name = "_ctypes.CField",
    .basicsize = sizeof(CFieldObject),
    .flags = (Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
              Py_TPFLAGS_IMMUTABLETYPE),
    .slots = cfield_slots,
};


/******************************************************************/
/*
  Accessor functions
*/

/* Derived from Modules/structmodule.c:
   Helper routine to get a Python integer and raise the appropriate error
   if it isn't one */

static int
get_long(PyObject *v, long *p)
{
    long x = PyLong_AsUnsignedLongMask(v);
    if (x == -1 && PyErr_Occurred())
        return -1;
    *p = x;
    return 0;
}

/* Same, but handling unsigned long */

static int
get_ulong(PyObject *v, unsigned long *p)
{
    unsigned long x = PyLong_AsUnsignedLongMask(v);
    if (x == (unsigned long)-1 && PyErr_Occurred())
        return -1;
    *p = x;
    return 0;
}

/* Same, but handling native long long. */

static int
get_longlong(PyObject *v, long long *p)
{
    long long x = PyLong_AsUnsignedLongLongMask(v);
    if (x == -1 && PyErr_Occurred())
        return -1;
    *p = x;
    return 0;
}

/* Same, but handling native unsigned long long. */

static int
get_ulonglong(PyObject *v, unsigned long long *p)
{
    unsigned long long x = PyLong_AsUnsignedLongLongMask(v);
    if (x == (unsigned long long)-1 && PyErr_Occurred())
        return -1;
    *p = x;
    return 0;
}

/*****************************************************************
 * Integer fields, with bitfield support
 */

/* how to decode the size field, for integer get/set functions */
static inline
Py_ssize_t LOW_BIT(Py_ssize_t offset) {
    return offset & 0xFFFF;
}
static inline
Py_ssize_t NUM_BITS(Py_ssize_t bitsize) {
    return bitsize >> 16;
}

/* Doesn't work if NUM_BITS(size) == 0, but it never happens in SET() call. */
#define BIT_MASK(type, size) (((((type)1 << (NUM_BITS(size) - 1)) - 1) << 1) + 1)

/* This macro CHANGES the first parameter IN PLACE. For proper sign handling,
   we must first shift left, then right.
*/
#define GET_BITFIELD(v, size)                                           \
    if (NUM_BITS(size)) {                                               \
        v <<= (sizeof(v)*8 - LOW_BIT(size) - NUM_BITS(size));           \
        v >>= (sizeof(v)*8 - NUM_BITS(size));                           \
    }

/* This macro RETURNS the first parameter with the bit field CHANGED. */
#define SET(type, x, v, size)                                                 \
    (NUM_BITS(size) ?                                                   \
     ( ( (type)x & ~(BIT_MASK(type, size) << LOW_BIT(size)) ) | ( ((type)v & BIT_MASK(type, size)) << LOW_BIT(size) ) ) \
     : (type)v)

#if SIZEOF_SHORT == 2
#  define SWAP_SHORT _Py_bswap16
#else
#  error "unsupported short size"
#endif

#if SIZEOF_INT == 4
#  define SWAP_INT _Py_bswap32
#else
#  error "unsupported int size"
#endif

#if SIZEOF_LONG == 4
#  define SWAP_LONG _Py_bswap32
#elif SIZEOF_LONG == 8
#  define SWAP_LONG _Py_bswap64
#else
#  error "unsupported long size"
#endif

#if SIZEOF_LONG_LONG == 8
#  define SWAP_LONG_LONG _Py_bswap64
#else
#  error "unsupported long long size"
#endif

/*****************************************************************
 * The setter methods return an object which must be kept alive, to keep the
 * data valid which has been stored in the memory block.  The ctypes object
 * instance inserts this object into its 'b_objects' list.
 *
 * For simple Python types like integers or characters, there is nothing that
 * has to been kept alive, so Py_None is returned in these cases.  But this
 * makes inspecting the 'b_objects' list, which is accessible from Python for
 * debugging, less useful.
 *
 * So, defining the _CTYPES_DEBUG_KEEP symbol returns the original value
 * instead of Py_None.
 */

#ifdef _CTYPES_DEBUG_KEEP
#define _RET(x) Py_INCREF(x); return x
#else
#define _RET(X) Py_RETURN_NONE
#endif

/*****************************************************************
 * integer accessor methods, supporting bit fields
 */

static PyObject *
b_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    if (get_long(value, &val) < 0)
        return NULL;
    *(signed char *)ptr = SET(signed char, *(signed char *)ptr, val, size);
    _RET(value);
}


static PyObject *
b_get(void *ptr, Py_ssize_t size)
{
    signed char val = *(signed char *)ptr;
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
B_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    if (get_ulong(value, &val) < 0)
        return NULL;
    *(unsigned char *)ptr = SET(unsigned char, *(unsigned char*)ptr, val, size);
    _RET(value);
}


static PyObject *
B_get(void *ptr, Py_ssize_t size)
{
    unsigned char val = *(unsigned char *)ptr;
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
h_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    short x;
    if (get_long(value, &val) < 0)
        return NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(short, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}


static PyObject *
h_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    short field;
    if (get_long(value, &val) < 0) {
        return NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_SHORT(field);
    field = SET(short, field, val, size);
    field = SWAP_SHORT(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}

static PyObject *
h_get(void *ptr, Py_ssize_t size)
{
    short val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromLong((long)val);
}

static PyObject *
h_get_sw(void *ptr, Py_ssize_t size)
{
    short val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_SHORT(val);
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
H_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    unsigned short x;
    if (get_ulong(value, &val) < 0)
        return NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(unsigned short, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
H_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    unsigned short field;
    if (get_ulong(value, &val) < 0) {
        return NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_SHORT(field);
    field = SET(unsigned short, field, val, size);
    field = SWAP_SHORT(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}


static PyObject *
H_get(void *ptr, Py_ssize_t size)
{
    unsigned short val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
H_get_sw(void *ptr, Py_ssize_t size)
{
    unsigned short val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_SHORT(val);
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
i_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    int x;
    if (get_long(value, &val) < 0)
        return NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(int, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
i_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    int field;
    if (get_long(value, &val) < 0) {
        return NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_INT(field);
    field = SET(int, field, val, size);
    field = SWAP_INT(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}


static PyObject *
i_get(void *ptr, Py_ssize_t size)
{
    int val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
i_get_sw(void *ptr, Py_ssize_t size)
{
    int val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_INT(val);
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

#ifndef MS_WIN32
/* http://msdn.microsoft.com/en-us/library/cc237864.aspx */
#define VARIANT_FALSE 0x0000
#define VARIANT_TRUE 0xFFFF
#endif
/* short BOOL - VARIANT_BOOL */
static PyObject *
vBOOL_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    switch (PyObject_IsTrue(value)) {
    case -1:
        return NULL;
    case 0:
        *(short int *)ptr = VARIANT_FALSE;
        _RET(value);
    default:
        *(short int *)ptr = VARIANT_TRUE;
        _RET(value);
    }
}

static PyObject *
vBOOL_get(void *ptr, Py_ssize_t size)
{
    return PyBool_FromLong((long)*(short int *)ptr);
}

static PyObject *
bool_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    switch (PyObject_IsTrue(value)) {
    case -1:
        return NULL;
    case 0:
        *(_Bool *)ptr = 0;
        _RET(value);
    default:
        *(_Bool *)ptr = 1;
        _RET(value);
    }
}

static PyObject *
bool_get(void *ptr, Py_ssize_t size)
{
    return PyBool_FromLong((long)*(_Bool *)ptr);
}

static PyObject *
I_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    unsigned int x;
    if (get_ulong(value, &val) < 0)
        return  NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(unsigned int, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
I_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    unsigned int field;
    if (get_ulong(value, &val) < 0) {
        return  NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_INT(field);
    field = SET(unsigned int, field, (unsigned int)val, size);
    field = SWAP_INT(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}


static PyObject *
I_get(void *ptr, Py_ssize_t size)
{
    unsigned int val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromUnsignedLong(val);
}

static PyObject *
I_get_sw(void *ptr, Py_ssize_t size)
{
    unsigned int val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_INT(val);
    GET_BITFIELD(val, size);
    return PyLong_FromUnsignedLong(val);
}

static PyObject *
l_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    long x;
    if (get_long(value, &val) < 0)
        return NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(long, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
l_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    long val;
    long field;
    if (get_long(value, &val) < 0) {
        return NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_LONG(field);
    field = SET(long, field, val, size);
    field = SWAP_LONG(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}


static PyObject *
l_get(void *ptr, Py_ssize_t size)
{
    long val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
l_get_sw(void *ptr, Py_ssize_t size)
{
    long val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_LONG(val);
    GET_BITFIELD(val, size);
    return PyLong_FromLong(val);
}

static PyObject *
L_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    unsigned long x;
    if (get_ulong(value, &val) < 0)
        return  NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(unsigned long, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
L_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long val;
    unsigned long field;
    if (get_ulong(value, &val) < 0) {
        return  NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_LONG(field);
    field = SET(unsigned long, field, val, size);
    field = SWAP_LONG(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}


static PyObject *
L_get(void *ptr, Py_ssize_t size)
{
    unsigned long val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromUnsignedLong(val);
}

static PyObject *
L_get_sw(void *ptr, Py_ssize_t size)
{
    unsigned long val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_LONG(val);
    GET_BITFIELD(val, size);
    return PyLong_FromUnsignedLong(val);
}

static PyObject *
q_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    long long val;
    long long x;
    if (get_longlong(value, &val) < 0)
        return NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(long long, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
q_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    long long val;
    long long field;
    if (get_longlong(value, &val) < 0) {
        return NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_LONG_LONG(field);
    field = SET(long long, field, val, size);
    field = SWAP_LONG_LONG(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}

static PyObject *
q_get(void *ptr, Py_ssize_t size)
{
    long long val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromLongLong(val);
}

static PyObject *
q_get_sw(void *ptr, Py_ssize_t size)
{
    long long val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_LONG_LONG(val);
    GET_BITFIELD(val, size);
    return PyLong_FromLongLong(val);
}

static PyObject *
Q_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long long val;
    unsigned long long x;
    if (get_ulonglong(value, &val) < 0)
        return NULL;
    memcpy(&x, ptr, sizeof(x));
    x = SET(long long, x, val, size);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
Q_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    unsigned long long val;
    unsigned long long field;
    if (get_ulonglong(value, &val) < 0) {
        return NULL;
    }
    memcpy(&field, ptr, sizeof(field));
    field = SWAP_LONG_LONG(field);
    field = SET(unsigned long long, field, val, size);
    field = SWAP_LONG_LONG(field);
    memcpy(ptr, &field, sizeof(field));
    _RET(value);
}

static PyObject *
Q_get(void *ptr, Py_ssize_t size)
{
    unsigned long long val;
    memcpy(&val, ptr, sizeof(val));
    GET_BITFIELD(val, size);
    return PyLong_FromUnsignedLongLong(val);
}

static PyObject *
Q_get_sw(void *ptr, Py_ssize_t size)
{
    unsigned long long val;
    memcpy(&val, ptr, sizeof(val));
    val = SWAP_LONG_LONG(val);
    GET_BITFIELD(val, size);
    return PyLong_FromUnsignedLongLong(val);
}

/*****************************************************************
 * non-integer accessor methods, not supporting bit fields
 */


static PyObject *
g_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    long double x;

    x = PyFloat_AsDouble(value);
    if (x == -1 && PyErr_Occurred())
        return NULL;
    memcpy(ptr, &x, sizeof(long double));
    _RET(value);
}

static PyObject *
g_get(void *ptr, Py_ssize_t size)
{
    long double val;
    memcpy(&val, ptr, sizeof(long double));
    return PyFloat_FromDouble(val);
}

static PyObject *
d_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    double x;

    x = PyFloat_AsDouble(value);
    if (x == -1 && PyErr_Occurred())
        return NULL;
    memcpy(ptr, &x, sizeof(double));
    _RET(value);
}

static PyObject *
d_get(void *ptr, Py_ssize_t size)
{
    double val;
    memcpy(&val, ptr, sizeof(val));
    return PyFloat_FromDouble(val);
}

#if defined(Py_HAVE_C_COMPLEX) && defined(Py_FFI_SUPPORT_C_COMPLEX)
static PyObject *
C_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    Py_complex c = PyComplex_AsCComplex(value);

    if (c.real == -1 && PyErr_Occurred()) {
        return NULL;
    }
    double complex x = CMPLX(c.real, c.imag);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
C_get(void *ptr, Py_ssize_t size)
{
    double complex x;

    memcpy(&x, ptr, sizeof(x));
    return PyComplex_FromDoubles(creal(x), cimag(x));
}

static PyObject *
E_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    Py_complex c = PyComplex_AsCComplex(value);

    if (c.real == -1 && PyErr_Occurred()) {
        return NULL;
    }
    float complex x = CMPLXF((float)c.real, (float)c.imag);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
E_get(void *ptr, Py_ssize_t size)
{
    float complex x;

    memcpy(&x, ptr, sizeof(x));
    return PyComplex_FromDoubles(crealf(x), cimagf(x));
}

static PyObject *
F_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    Py_complex c = PyComplex_AsCComplex(value);

    if (c.real == -1 && PyErr_Occurred()) {
        return NULL;
    }
    long double complex x = CMPLXL(c.real, c.imag);
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
F_get(void *ptr, Py_ssize_t size)
{
    long double complex x;

    memcpy(&x, ptr, sizeof(x));
    return PyComplex_FromDoubles((double)creall(x), (double)cimagl(x));
}
#endif

static PyObject *
d_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    double x;

    x = PyFloat_AsDouble(value);
    if (x == -1 && PyErr_Occurred())
        return NULL;
#ifdef WORDS_BIGENDIAN
    if (PyFloat_Pack8(x, ptr, 1))
        return NULL;
#else
    if (PyFloat_Pack8(x, ptr, 0))
        return NULL;
#endif
    _RET(value);
}

static PyObject *
d_get_sw(void *ptr, Py_ssize_t size)
{
#ifdef WORDS_BIGENDIAN
    return PyFloat_FromDouble(PyFloat_Unpack8(ptr, 1));
#else
    return PyFloat_FromDouble(PyFloat_Unpack8(ptr, 0));
#endif
}

static PyObject *
f_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    float x;

    x = (float)PyFloat_AsDouble(value);
    if (x == -1 && PyErr_Occurred())
        return NULL;
    memcpy(ptr, &x, sizeof(x));
    _RET(value);
}

static PyObject *
f_get(void *ptr, Py_ssize_t size)
{
    float val;
    memcpy(&val, ptr, sizeof(val));
    return PyFloat_FromDouble(val);
}

static PyObject *
f_set_sw(void *ptr, PyObject *value, Py_ssize_t size)
{
    float x;

    x = (float)PyFloat_AsDouble(value);
    if (x == -1 && PyErr_Occurred())
        return NULL;
#ifdef WORDS_BIGENDIAN
    if (PyFloat_Pack4(x, ptr, 1))
        return NULL;
#else
    if (PyFloat_Pack4(x, ptr, 0))
        return NULL;
#endif
    _RET(value);
}

static PyObject *
f_get_sw(void *ptr, Py_ssize_t size)
{
#ifdef WORDS_BIGENDIAN
    return PyFloat_FromDouble(PyFloat_Unpack4(ptr, 1));
#else
    return PyFloat_FromDouble(PyFloat_Unpack4(ptr, 0));
#endif
}

/*
  py_object refcounts:

  1. If we have a py_object instance, O_get must Py_INCREF the returned
  object, of course.  If O_get is called from a function result, no py_object
  instance is created - so callproc.c::GetResult has to call Py_DECREF.

  2. The memory block in py_object owns a refcount.  So, py_object must call
  Py_DECREF on destruction.  Maybe only when b_needsfree is non-zero.
*/
static PyObject *
O_get(void *ptr, Py_ssize_t size)
{
    PyObject *ob = *(PyObject **)ptr;
    if (ob == NULL) {
        if (!PyErr_Occurred())
            /* Set an error if not yet set */
            PyErr_SetString(PyExc_ValueError,
                            "PyObject is NULL");
        return NULL;
    }
    return Py_NewRef(ob);
}

static PyObject *
O_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    /* Hm, does the memory block need it's own refcount or not? */
    *(PyObject **)ptr = value;
    return Py_NewRef(value);
}


static PyObject *
c_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    if (PyBytes_Check(value)) {
        if (PyBytes_GET_SIZE(value) != 1) {
            PyErr_Format(PyExc_TypeError,
                        "one character bytes, bytearray, or an integer "
                        "in range(256) expected, not bytes of length %zd",
                        PyBytes_GET_SIZE(value));
            return NULL;
        }
        *(char *)ptr = PyBytes_AS_STRING(value)[0];
        _RET(value);
    }
    if (PyByteArray_Check(value)) {
        if (PyByteArray_GET_SIZE(value) != 1) {
            PyErr_Format(PyExc_TypeError,
                        "one character bytes, bytearray, or an integer "
                        "in range(256) expected, not bytearray of length %zd",
                        PyByteArray_GET_SIZE(value));
            return NULL;
        }
        *(char *)ptr = PyByteArray_AS_STRING(value)[0];
        _RET(value);
    }
    if (PyLong_Check(value)) {
        int overflow;
        long longval = PyLong_AsLongAndOverflow(value, &overflow);
        if (longval == -1 && PyErr_Occurred()) {
            return NULL;
        }
        if (overflow || longval < 0 || longval >= 256) {
            PyErr_SetString(PyExc_TypeError, "integer not in range(256)");
            return NULL;
        }
        *(char *)ptr = (char)longval;
        _RET(value);
    }
    PyErr_Format(PyExc_TypeError,
                 "one character bytes, bytearray, or an integer "
                 "in range(256) expected, not %T",
                 value);
    return NULL;
}


static PyObject *
c_get(void *ptr, Py_ssize_t size)
{
    return PyBytes_FromStringAndSize((char *)ptr, 1);
}

/* u - a single wchar_t character */
static PyObject *
u_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    Py_ssize_t len;
    wchar_t chars[2];
    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "a unicode character expected, not instance of %T",
                     value);
        return NULL;
    }

    len = PyUnicode_AsWideChar(value, chars, 2);
    if (len != 1) {
        if (PyUnicode_GET_LENGTH(value) != 1) {
            PyErr_Format(PyExc_TypeError,
                         "a unicode character expected, not a string of length %zd",
                         PyUnicode_GET_LENGTH(value));
        }
        else {
            PyErr_Format(PyExc_TypeError,
                         "the string %A cannot be converted to a single wchar_t character",
                         value);
        }
        return NULL;
    }

    *(wchar_t *)ptr = chars[0];

    _RET(value);
}


static PyObject *
u_get(void *ptr, Py_ssize_t size)
{
    return PyUnicode_FromWideChar((wchar_t *)ptr, 1);
}

/* U - a unicode string */
static PyObject *
U_get(void *ptr, Py_ssize_t size)
{
    Py_ssize_t len;
    wchar_t *p;

    size /= sizeof(wchar_t); /* we count character units here, not bytes */

    /* We need 'result' to be able to count the characters with wcslen,
       since ptr may not be NUL terminated.  If the length is smaller (if
       it was actually NUL terminated, we construct a new one and throw
       away the result.
    */
    /* chop off at the first NUL character, if any. */
    p = (wchar_t*)ptr;
    for (len = 0; len < size; ++len) {
        if (!p[len])
            break;
    }

    return PyUnicode_FromWideChar((wchar_t *)ptr, len);
}

static PyObject *
U_set(void *ptr, PyObject *value, Py_ssize_t length)
{
    /* It's easier to calculate in characters than in bytes */
    length /= sizeof(wchar_t);

    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                        "unicode string expected instead of %s instance",
                        Py_TYPE(value)->tp_name);
        return NULL;
    }

    Py_ssize_t size = PyUnicode_AsWideChar(value, NULL, 0);
    if (size < 0) {
        return NULL;
    }
    // PyUnicode_AsWideChar() returns number of wchars including trailing null byte,
    // when it is called with NULL.
    size--;
    assert(size >= 0);
    if (size > length) {
        PyErr_Format(PyExc_ValueError,
                     "string too long (%zd, maximum length %zd)",
                     size, length);
        return NULL;
    }
    if (PyUnicode_AsWideChar(value, (wchar_t *)ptr, length) == -1) {
        return NULL;
    }

    return Py_NewRef(value);
}


static PyObject *
s_get(void *ptr, Py_ssize_t size)
{
    Py_ssize_t i;
    char *p;

    p = (char *)ptr;
    for (i = 0; i < size; ++i) {
        if (*p++ == '\0')
            break;
    }

    return PyBytes_FromStringAndSize((char *)ptr, (Py_ssize_t)i);
}

static PyObject *
s_set(void *ptr, PyObject *value, Py_ssize_t length)
{
    const char *data;
    Py_ssize_t size;

    if(!PyBytes_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "expected bytes, %s found",
                     Py_TYPE(value)->tp_name);
        return NULL;
    }

    data = PyBytes_AS_STRING(value);
    // bpo-39593: Use strlen() to truncate the string at the first null character.
    size = strlen(data);

    if (size < length) {
        /* This will copy the terminating NUL character
         * if there is space for it.
         */
        ++size;
    } else if (size > length) {
        PyErr_Format(PyExc_ValueError,
                     "bytes too long (%zd, maximum length %zd)",
                     size, length);
        return NULL;
    }
    /* Also copy the terminating NUL character if there is space */
    memcpy((char *)ptr, data, size);

    _RET(value);
}

static PyObject *
z_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    if (value == Py_None) {
        *(char **)ptr = NULL;
        return Py_NewRef(value);
    }
    if (PyBytes_Check(value)) {
        *(const char **)ptr = PyBytes_AsString(value);
        return Py_NewRef(value);
    } else if (PyLong_Check(value)) {
#if SIZEOF_VOID_P == SIZEOF_LONG_LONG
        *(char **)ptr = (char *)PyLong_AsUnsignedLongLongMask(value);
#else
        *(char **)ptr = (char *)PyLong_AsUnsignedLongMask(value);
#endif
        _RET(value);
    }
    PyErr_Format(PyExc_TypeError,
                 "bytes or integer address expected instead of %s instance",
                 Py_TYPE(value)->tp_name);
    return NULL;
}

static PyObject *
z_get(void *ptr, Py_ssize_t size)
{
    /* XXX What about invalid pointers ??? */
    if (*(void **)ptr) {
        return PyBytes_FromStringAndSize(*(char **)ptr,
                                         strlen(*(char **)ptr));
    } else {
        Py_RETURN_NONE;
    }
}

static PyObject *
Z_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    PyObject *keep;
    wchar_t *buffer;
    Py_ssize_t bsize;

    if (value == Py_None) {
        *(wchar_t **)ptr = NULL;
        return Py_NewRef(value);
    }
    if (PyLong_Check(value)) {
#if SIZEOF_VOID_P == SIZEOF_LONG_LONG
        *(wchar_t **)ptr = (wchar_t *)PyLong_AsUnsignedLongLongMask(value);
#else
        *(wchar_t **)ptr = (wchar_t *)PyLong_AsUnsignedLongMask(value);
#endif
        Py_RETURN_NONE;
    }
    if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                     "unicode string or integer address expected instead of %s instance",
                     Py_TYPE(value)->tp_name);
        return NULL;
    }

    /* We must create a wchar_t* buffer from the unicode object,
       and keep it alive */
    buffer = PyUnicode_AsWideCharString(value, &bsize);
    if (!buffer)
        return NULL;
    keep = PyCapsule_New(buffer, CTYPES_CFIELD_CAPSULE_NAME_PYMEM, pymem_destructor);
    if (!keep) {
        PyMem_Free(buffer);
        return NULL;
    }
    *(wchar_t **)ptr = buffer;
    return keep;
}

static PyObject *
Z_get(void *ptr, Py_ssize_t size)
{
    wchar_t *p;
    p = *(wchar_t **)ptr;
    if (p) {
        return PyUnicode_FromWideChar(p, wcslen(p));
    } else {
        Py_RETURN_NONE;
    }
}


#ifdef MS_WIN32
static PyObject *
BSTR_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    BSTR bstr;

    /* convert value into a PyUnicodeObject or NULL */
    if (Py_None == value) {
        value = NULL;
    } else if (!PyUnicode_Check(value)) {
        PyErr_Format(PyExc_TypeError,
                        "unicode string expected instead of %s instance",
                        Py_TYPE(value)->tp_name);
        return NULL;
    }

    /* create a BSTR from value */
    if (value) {
        Py_ssize_t wsize;
        wchar_t *wvalue = PyUnicode_AsWideCharString(value, &wsize);
        if (wvalue == NULL) {
            return NULL;
        }
        if ((unsigned) wsize != wsize) {
            PyErr_SetString(PyExc_ValueError, "String too long for BSTR");
            PyMem_Free(wvalue);
            return NULL;
        }
        bstr = SysAllocStringLen(wvalue, (unsigned)wsize);
        PyMem_Free(wvalue);
    } else
        bstr = NULL;

    /* free the previous contents, if any */
    if (*(BSTR *)ptr)
        SysFreeString(*(BSTR *)ptr);

    /* and store it */
    *(BSTR *)ptr = bstr;

    /* We don't need to keep any other object */
    _RET(value);
}


static PyObject *
BSTR_get(void *ptr, Py_ssize_t size)
{
    BSTR p;
    p = *(BSTR *)ptr;
    if (p)
        return PyUnicode_FromWideChar(p, SysStringLen(p));
    else {
        /* Hm, it seems NULL pointer and zero length string are the
           same in BSTR, see Don Box, p 81
        */
        Py_RETURN_NONE;
    }
}
#endif

static PyObject *
P_set(void *ptr, PyObject *value, Py_ssize_t size)
{
    void *v;
    if (value == Py_None) {
        *(void **)ptr = NULL;
        _RET(value);
    }

    if (!PyLong_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot be converted to pointer");
        return NULL;
    }

#if SIZEOF_VOID_P <= SIZEOF_LONG
    v = (void *)PyLong_AsUnsignedLongMask(value);
#else
#if SIZEOF_LONG_LONG < SIZEOF_VOID_P
#   error "PyLong_AsVoidPtr: sizeof(long long) < sizeof(void*)"
#endif
    v = (void *)PyLong_AsUnsignedLongLongMask(value);
#endif

    if (PyErr_Occurred())
        return NULL;

    *(void **)ptr = v;
    _RET(value);
}

static PyObject *
P_get(void *ptr, Py_ssize_t size)
{
    if (*(void **)ptr == NULL) {
        Py_RETURN_NONE;
    }
    return PyLong_FromVoidPtr(*(void **)ptr);
}

static struct fielddesc formattable[] = {
    { 's', s_set, s_get, NULL},
    { 'b', b_set, b_get, NULL},
    { 'B', B_set, B_get, NULL},
    { 'c', c_set, c_get, NULL},
    { 'd', d_set, d_get, NULL, d_set_sw, d_get_sw},
#if defined(Py_HAVE_C_COMPLEX) && defined(Py_FFI_SUPPORT_C_COMPLEX)
    { 'C', C_set, C_get, NULL},
    { 'E', E_set, E_get, NULL},
    { 'F', F_set, F_get, NULL},
#endif
    { 'g', g_set, g_get, NULL},
    { 'f', f_set, f_get, NULL, f_set_sw, f_get_sw},
    { 'h', h_set, h_get, NULL, h_set_sw, h_get_sw},
    { 'H', H_set, H_get, NULL, H_set_sw, H_get_sw},
    { 'i', i_set, i_get, NULL, i_set_sw, i_get_sw},
    { 'I', I_set, I_get, NULL, I_set_sw, I_get_sw},
    { 'l', l_set, l_get, NULL, l_set_sw, l_get_sw},
    { 'L', L_set, L_get, NULL, L_set_sw, L_get_sw},
    { 'q', q_set, q_get, NULL, q_set_sw, q_get_sw},
    { 'Q', Q_set, Q_get, NULL, Q_set_sw, Q_get_sw},
    { 'P', P_set, P_get, NULL},
    { 'z', z_set, z_get, NULL},
    { 'u', u_set, u_get, NULL},
    { 'U', U_set, U_get, NULL},
    { 'Z', Z_set, Z_get, NULL},
#ifdef MS_WIN32
    { 'X', BSTR_set, BSTR_get, NULL},
#endif
    { 'v', vBOOL_set, vBOOL_get, NULL},
#if SIZEOF__BOOL == SIZEOF_INT
    { '?', bool_set, bool_get, NULL, I_set_sw, I_get_sw},
#elif SIZEOF__BOOL == SIZEOF_LONG
    { '?', bool_set, bool_get, NULL, L_set_sw, L_get_sw},
#elif SIZEOF__BOOL == SIZEOF_LONG_LONG
    { '?', bool_set, bool_get, NULL, Q_set_sw, Q_get_sw},
#else
    { '?', bool_set, bool_get, NULL},
#endif /* SIZEOF__BOOL */
    { 'O', O_set, O_get, NULL},
    { 0, NULL, NULL, NULL},
};

/*
  Ideas: Implement VARIANT in this table, using 'V' code.
  Use '?' as code for BOOL.
*/

/* Delayed initialization. Windows cannot statically reference dynamically
   loaded addresses from DLLs. */
void
_ctypes_init_fielddesc(void)
{
    struct fielddesc *fd = formattable;
    for (; fd->code; ++fd) {
        switch (fd->code) {
        case 's': fd->pffi_type = &ffi_type_pointer; break;
        case 'b': fd->pffi_type = &ffi_type_schar; break;
        case 'B': fd->pffi_type = &ffi_type_uchar; break;
        case 'c': fd->pffi_type = &ffi_type_schar; break;
        case 'd': fd->pffi_type = &ffi_type_double; break;
#if defined(Py_HAVE_C_COMPLEX) && defined(Py_FFI_SUPPORT_C_COMPLEX)
        case 'C': fd->pffi_type = &ffi_type_complex_double; break;
        case 'E': fd->pffi_type = &ffi_type_complex_float; break;
        case 'F': fd->pffi_type = &ffi_type_complex_longdouble; break;
#endif
        case 'g': fd->pffi_type = &ffi_type_longdouble; break;
        case 'f': fd->pffi_type = &ffi_type_float; break;
        case 'h': fd->pffi_type = &ffi_type_sshort; break;
        case 'H': fd->pffi_type = &ffi_type_ushort; break;
        case 'i': fd->pffi_type = &ffi_type_sint; break;
        case 'I': fd->pffi_type = &ffi_type_uint; break;
        /* XXX Hm, sizeof(int) == sizeof(long) doesn't hold on every platform */
        /* As soon as we can get rid of the type codes, this is no longer a problem */
    #if SIZEOF_LONG == 4
        case 'l': fd->pffi_type = &ffi_type_sint32; break;
        case 'L': fd->pffi_type = &ffi_type_uint32; break;
    #elif SIZEOF_LONG == 8
        case 'l': fd->pffi_type = &ffi_type_sint64; break;
        case 'L': fd->pffi_type = &ffi_type_uint64; break;
    #else
        #error
    #endif
    #if SIZEOF_LONG_LONG == 8
        case 'q': fd->pffi_type = &ffi_type_sint64; break;
        case 'Q': fd->pffi_type = &ffi_type_uint64; break;
    #else
        #error
    #endif
        case 'P': fd->pffi_type = &ffi_type_pointer; break;
        case 'z': fd->pffi_type = &ffi_type_pointer; break;
        case 'u':
            if (sizeof(wchar_t) == sizeof(short))
                fd->pffi_type = &ffi_type_sshort;
            else if (sizeof(wchar_t) == sizeof(int))
                fd->pffi_type = &ffi_type_sint;
            else if (sizeof(wchar_t) == sizeof(long))
                fd->pffi_type = &ffi_type_slong;
            else
                Py_UNREACHABLE();
            break;
        case 'U': fd->pffi_type = &ffi_type_pointer; break;
        case 'Z': fd->pffi_type = &ffi_type_pointer; break;
    #ifdef MS_WIN32
        case 'X': fd->pffi_type = &ffi_type_pointer; break;
    #endif
        case 'v': fd->pffi_type = &ffi_type_sshort; break;
    #if SIZEOF__BOOL == 1
        case '?': fd->pffi_type = &ffi_type_uchar; break; /* Also fallback for no native _Bool support */
    #elif SIZEOF__BOOL == SIZEOF_SHORT
        case '?': fd->pffi_type = &ffi_type_ushort; break;
    #elif SIZEOF__BOOL == SIZEOF_INT
        case '?': fd->pffi_type = &ffi_type_uint; break;
    #elif SIZEOF__BOOL == SIZEOF_LONG
        case '?': fd->pffi_type = &ffi_type_ulong; break;
    #elif SIZEOF__BOOL == SIZEOF_LONG_LONG
        case '?': fd->pffi_type = &ffi_type_ulong; break;
    #endif /* SIZEOF__BOOL */
        case 'O': fd->pffi_type = &ffi_type_pointer; break;
        default:
            Py_UNREACHABLE();
        }
    }

}

struct fielddesc *
_ctypes_get_fielddesc(const char *fmt)
{
    static int initialized = 0;
    struct fielddesc *table = formattable;

    if (!initialized) {
        initialized = 1;
        _ctypes_init_fielddesc();
    }

    for (; table->code; ++table) {
        if (table->code == fmt[0])
            return table;
    }
    return NULL;
}

/*---------------- EOF ----------------*/

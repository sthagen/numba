/*
 * Copyright (c) 2012 Continuum Analytics, Inc. 
 * All Rights reserved.
 */

/* Python include */
#include "Python.h"
#include "numpy/ndarrayobject.h"
#include "numpy/ufuncobject.h"

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#endif

/* Deallocate the PyArray_malloc calls */
static void
dyn_dealloc(PyUFuncObject *self)
{
    if (self->functions)
        PyArray_free(self->functions);
    if (self->types)
        PyArray_free(self->types);
    if (self->data)
        PyArray_free(self->data);
    Py_TYPE(self)->tp_dealloc((PyObject *)self);    
}


NPY_NO_EXPORT PyTypeObject PyDynUFunc_Type = {
#if defined(NPY_PY3K)
    PyVarObject_HEAD_INIT(NULL, 0)
#else
    PyObject_HEAD_INIT(NULL)
    0,                                          /* ob_size */
#endif
    "numbapro.dyn_ufunc",                       /* tp_name*/
    sizeof(PyUFuncObject),                      /* tp_basicsize*/
    0,                                          /* tp_itemsize */
    /* methods */
    (destructor) dyn_dealloc,                                /* tp_dealloc */
    0,                                          /* tp_print */
    0,                                          /* tp_getattr */
    0,                                          /* tp_setattr */
#if defined(NPY_PY3K)
    0,                                          /* tp_reserved */
#else
    0,                                          /* tp_compare */
#endif
    0,                                          /* tp_repr */
    0,                                          /* tp_as_number */
    0,                                          /* tp_as_sequence */
    0,                                          /* tp_as_mapping */
    0,                                          /* tp_hash */
    0,                                          /* tp_call */
    0,                                          /* tp_str */
    0,                                          /* tp_getattro */
    0,                                          /* tp_setattro */
    0,                                          /* tp_as_buffer */
    0,                                          /* tp_flags */
    0,                                          /* tp_doc */
    0,                                          /* tp_traverse */
    0,                                          /* tp_clear */
    0,                                          /* tp_richcompare */
    0,                                          /* tp_weaklistoffset */
    0,                                          /* tp_iter */
    0,                                          /* tp_iternext */
    0,                                          /* tp_methods */
    0,                                          /* tp_members */
    0,                                          /* tp_getset */
    0,                                          /* tp_base */
    0,                                          /* tp_dict */
    0,                                          /* tp_descr_get */
    0,                                          /* tp_descr_set */
    0,                                          /* tp_dictoffset */
    0,                                          /* tp_init */
    0,                                          /* tp_alloc */
    0,                                          /* tp_new */
    0,                                          /* tp_free */
    0,                                          /* tp_is_gc */
    0,                                          /* tp_bases */
    0,                                          /* tp_mro */
    0,                                          /* tp_cache */
    0,                                          /* tp_subclasses */
    0,                                          /* tp_weaklist */
    0,                                          /* tp_del */
#if PY_VERSION_HEX >= 0x02060000
    0,                                          /* tp_version_tag */
#endif
};

PyObject *
PyDynUFunc_FromFuncAndData(PyUFuncGenericFunction *func, void **data,
                           char *types, int ntypes,
                           int nin, int nout, int identity,
                           char *name, char *doc, int check_return)
{
    PyObject *ufunc;
    ufunc = PyUFunc_FromFuncAndData(func, data, types, ntypes, nin, nout,
                                    identity, name, doc, check_return);
    
    /* Kind of a gross-hack  */
    Py_TYPE(ufunc) = &PyDynUFunc_Type;
    return ufunc;
}

static PyObject *
ufunc_fromfunc(PyObject *NPY_UNUSED(dummy), PyObject *args) {

    unsigned long func_address;
    int nin, nout;
    int nfuncs, ntypes, ndata;
    PyObject *func_list;
    PyObject *type_list;
    PyObject *data_list;
    PyObject *func_obj;
    PyObject *type_obj;
    PyObject *data_obj;
    PyObject *object=NULL; /* object to hold on to while ufunc is alive */
    int i, j; 
    int custom_dtype = 0;

    if (!PyArg_ParseTuple(args, "O!O!iiO|O", &PyList_Type, &func_list, &PyList_Type, &type_list, &nin, &nout, &data_list, &object)) {
        return NULL;
    }

    nfuncs = PyList_Size(func_list);

    ntypes = PyList_Size(type_list);
    if (ntypes != nfuncs) {
        PyErr_SetString(PyExc_TypeError, "length of types list must be same as length of function pointer list");
        return NULL;
    }

    ndata = PyList_Size(data_list);
    if (ndata != nfuncs) {
        PyErr_SetString(PyExc_TypeError, "length of data pointer list must be same as length of function pointer list");
        return NULL;
    }

    PyUFuncGenericFunction *funcs = PyArray_malloc(nfuncs * sizeof(PyUFuncGenericFunction));
    if (funcs == NULL) {
        return NULL;
    }

    /* build function pointer array */
    for (i = 0; i < nfuncs; i++) {
        func_obj = PyList_GetItem(func_list, i);
        /* Function pointers are passed in as long objects.
           Is there a better way to do this? */
        if (PyLong_Check(func_obj)) {
            funcs[i] = (PyUFuncGenericFunction)PyLong_AsLong(func_obj);
        }
        else {
            PyErr_SetString(PyExc_TypeError, "function pointer must be long object, or None");
            return NULL;
        }
    }

    int *types = PyArray_malloc(nfuncs * (nin+nout) * sizeof(int));
    if (types == NULL) {
        return NULL;
    }

    /* build function signatures array */
    for (i = 0; i < nfuncs; i++) {
        type_obj = PyList_GetItem(type_list, i);
        
        for (j = 0; j < (nin+nout); j++) {
            types[i*(nin+nout) + j] = PyLong_AsLong(PyList_GetItem(type_obj, j));
            int dtype_num = PyLong_AsLong(PyList_GetItem(type_obj, j));
            if (dtype_num >= NPY_USERDEF) {
                custom_dtype = dtype_num;
            }
        }
    }

    void **data = PyArray_malloc(nfuncs * sizeof(void *));
    if (data == NULL) {
        return NULL;
    }

    /* build function data pointers array */
    for (i = 0; i < nfuncs; i++) {
        if (PyList_Check(data_list)) {
            data_obj = PyList_GetItem(data_list, i);
            if (PyLong_Check(data_obj)) {
                data[i] = (void*)PyLong_AsLong(data_obj);
            }
            else if (data_obj == Py_None) {
                data[i] = NULL;
            }
            else {
                PyErr_SetString(PyExc_TypeError, "data pointer must be long object, or None");
                return NULL;
            }
        }
        else if (data_list == Py_None) {
            data[i] = NULL;
        }
        else {
            PyErr_SetString(PyExc_TypeError, "data pointers argument must be a list of void pointers, or None");
            return NULL;
        }
    }

    PyObject *ufunc;
    if (!custom_dtype) {
        char *char_types = PyArray_malloc(nfuncs * (nin+nout) * sizeof(char));
        for (i = 0; i < nfuncs; i++) {
            for (j = 0; j < (nin+nout); j++) {
                char_types[i*(nin+nout) + j] = (char)types[i*(nin+nout) + j];
            }
        }
        PyArray_free(types);
        ufunc = PyDynUFunc_FromFuncAndData((PyUFuncGenericFunction*)funcs,data,(char*)char_types,nfuncs,nin,nout,PyUFunc_None,"test",(char*)"test",0);
        Py_XINCREF(object);
        ((PyUFuncObject *)ufunc)->obj = object;
    }
    else {
        ufunc = PyDynUFunc_FromFuncAndData(0,0,0,0,nin,nout,PyUFunc_None,"test",(char*)"test",0);
        PyUFunc_RegisterLoopForType((PyUFuncObject*)ufunc,custom_dtype,funcs[0],types,0);
        PyArray_free(funcs);
        PyArray_free(types);
        PyArray_free(data);
        Py_XINCREF(object);
        ((PyUFuncObject *)ufunc)->obj = object;
    }

    return ufunc;
}



static PyMethodDef ext_methods[] = {

#ifdef IS_PY3K
    {"fromfunc", (PyCFunction) ufunc_fromfunc, METH_VARARGS, NULL},
#else
    {"fromfunc", ufunc_fromfunc, METH_VARARGS, NULL},
#endif
    { NULL }
};


#if IS_PY3K

struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "_internal",
    NULL,
    -1,
    ext_methods,
    NULL, NULL, NULL, NULL
};
#endif

#ifdef IS_PY3K
#define RETVAL m
PyObject *
PyInit__internal(void)
#else
#define RETVAL
PyMODINIT_FUNC
init_internal(void)
#endif
{
    PyObject *m, *d;

    import_array();
    import_umath();

#ifdef IS_PY3K
    m = PyModule_Create( &module_def );
#else
    m = Py_InitModule("_internal", ext_methods);
#endif

    /* Inherit the dynamic UFunc from UFunc */
    PyDynUFunc_Type.tp_base = &PyUFunc_Type;
    if (PyType_Ready(&PyDynUFunc_Type) < 0)
        return RETVAL;

    return RETVAL;
}


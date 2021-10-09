#include <Python.h>
#include <locale>

#include "diff-match-patch-cpp-stl/diff_match_patch.h"

/* Shims for Python 2 / Python 3 */

#if PY_MAJOR_VERSION == 3
    // Upgrade these types.
    #define PyString_FromString PyUnicode_FromString
    #define PyInt_FromLong PyLong_FromLong
#endif

#if PY_MAJOR_VERSION == 2
    #define UNICODESTRING_FORMAT_SYMBOL 'u'
    #define BYTESTRING_FORMAT_SYMBOL 's'
#else
    #define UNICODESTRING_FORMAT_SYMBOL 'U'
    #define BYTESTRING_FORMAT_SYMBOL 'y'
#endif

// template traits class
template <char FMTSPEC>
struct call_traits {
};

#if PY_MAJOR_VERSION == 2
// Python 2 string
template <>
struct call_traits<'s'> {
    // PyArg_ParseTuple for 's' gives a 'char*', and we'll
    // convert that to a std::string using a cast operator.
    typedef char* PY_ARG_TYPE;
    typedef std::string STL_STRING_TYPE;

    // Use the operator cast to convert char*s to std::strings.
    static std::string to_string(char* value) { return (std::string)value; }

    // Create PyString from underlying char array
    static PyObject* from_string(std::string& value) {
        return PyString_FromStringAndSize(value.data(), value.size());
    }

    // Convert std::strings to char*s
    static const char* to_bytes(std::string& value) {
        return value.c_str();
    }
};
#endif

#if PY_MAJOR_VERSION == 3
// Python 3 bytes
template <>
struct call_traits<'y'> {
    // PyArg_ParseTuple for 'y' gives a 'char*', and we'll
    // convert that to a std::string using a cast operator.
    typedef char* PY_ARG_TYPE;
    typedef std::string STL_STRING_TYPE;

    // Use the operator cast to convert char*s to std::strings.
    static std::string to_string(char* value) { return (std::string)value; }

    // Create PyString from underlying char array
    static PyObject* from_string(std::string& value) {
        return PyUnicode_FromStringAndSize(value.data(), value.size());
    }

    // Convert std::strings to char*s
    static const char* to_bytes(std::string& value) {
        return value.c_str();
    }
};
#endif

#if PY_MAJOR_VERSION == 2
// Python 2 unicode
template <>
struct call_traits<'u'> {
    // PyArg_ParseTuple for 'u' gives a 'Py_UNICODE*'. That's a
    // typedef for wchar_t, unsigned short (UCS2) or unsigned long (UCS4).
    // On Ubuntu, it seems to be a typedef for wchar_t.
    // On Macs, the default build has it as a typedef for UCS2 (a "narrow" build).
    // With Python 3.3 and forward, it is always a typedef for wchar_t.
    typedef Py_UNICODE* PY_ARG_TYPE;
    typedef std::wstring STL_STRING_TYPE;

    // Convert Py_UNICODE* to std::wstring....
    static std::wstring to_string(Py_UNICODE* value) {
        // If Py_UNICODE is the same width as wchar_t, then just do a few casts.
        // Hopefully wchar_t is unsigned, but it may not matter anyway. When
        // Py_UNICODE actually *is* wchar_t, the casts are unnecessary, but we
        // want this to compile in other environments as well.
        if (sizeof(Py_UNICODE) == sizeof(wchar_t))
            return (std::wstring)(wchar_t*)value;

        // In other cases, cast each character to wchar_t. Technically this
        // means that when a surrogate pair appears in the UTF-16 source, we
        // will treat it as two "unpaired surrogate" codepoints, which are
        // illegal in UTF-32. But the library doesn't care, and we will just be
        // converting back to UTF-16 later.
        //
        // There is a risk that the unpaired surrogates will end up in
        // different diff blocks, but this is par for the course for narrow
        // builds of Python. (difflib has the same problem.)
        size_t len = 0;
        while (value[len] != 0)
            len++;
        wchar_t* buf = (wchar_t*)malloc(sizeof(wchar_t) * (len + 1));
        assert(buf);
        buf[len] = '\0';
        for (size_t i = 0; i < len; i++)
            buf[i] = (wchar_t)value[i];
        std::wstring ret = (std::wstring)buf;
        free(buf);
        return ret;
    }

    static PyObject* from_string(std::wstring value) {
        // Wide build--just cast underlying char_t array.
        if (sizeof(Py_UNICODE) == sizeof(wchar_t))
            return PyUnicode_FromUnicode((Py_UNICODE*)value.data(), value.size());

        // Narrow build--cast to 16-bit. This is not the normal way to convert
        // UTF-32 to UTF-16, but since we got the wstring by casting UTF-16
        // chars it is fine in this case (modulo the unpaired surrogate issues
        // which we are intentionally not addressing here--see comments in
        // to_string).
        size_t len = value.size();
        Py_UNICODE* buf = (Py_UNICODE*)malloc(sizeof(Py_UNICODE) * (len + 1));
        assert(buf);
        buf[len] = '\0';
        for (size_t i = 0; i < len; i++)
            buf[i] = (Py_UNICODE)value[i];
        PyObject* ret = PyUnicode_FromUnicode(buf, len);
        free(buf);
        return ret;
    }

    // just return a dummy error, because doing the locale conversion manually is complicated
    static const char* to_bytes(std::wstring& value) {
        return "Unspecified error";
    }
};
#endif

#if PY_MAJOR_VERSION == 3
// Python 3.3+ unicode
template <>
struct call_traits<'U'> {
    typedef PyObject* PY_ARG_TYPE;
    typedef std::wstring STL_STRING_TYPE;

    // Convert PyObject* to std::wstring....
    static std::wstring to_string(PyObject* value) {
        Py_ssize_t size;
        wchar_t* str = PyUnicode_AsWideCharString(value, &size);
        std::wstring string = std::wstring(str, size);
        PyMem_Free(str);
        return string;
    }

    static PyObject* from_string(std::wstring value) {
        return PyUnicode_FromWideChar(value.data(), value.size());
    }

/* Python 3.5 introduced Py_EncodeLocale for converting wchar_t* to char*.
   If we're on Python 3.4, just return a dummy error, because
   doing the locale conversion manually is complicated. */
#if PY_MINOR_VERSION <= 4
    static const char* to_bytes(std::wstring& value) {
        return "Unspecified error";
    }
#else
    static const char* to_bytes(std::wstring& value) {
        return Py_EncodeLocale(value.c_str(), NULL);
    }
#endif
};
#endif

// COMPUTATIONAL FUNCTIONS

template <char FMTSPEC>
static PyObject *
diff_match_patch__diff__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typedef call_traits<FMTSPEC> traits;
    typename traits::PY_ARG_TYPE a, b;
    float timelimit = 0.0;
    int checklines = 1;
    int cleanupSemantic = 1;
    int counts_only = 1;
    int as_patch = 0;
    char format_spec[64];

    static char *kwlist[] = {
        strdup("left_document"),
        strdup("right_document"),
        strdup("timelimit"),
        strdup("checklines"),
        strdup("cleanup_semantic"),
        strdup("counts_only"),
        strdup("as_patch"),
        NULL };

    sprintf(format_spec, "%c%c|fbbbb", FMTSPEC, FMTSPEC);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &a, &b,
                                     &timelimit, &checklines, &cleanupSemantic,
                                     &counts_only, &as_patch))
        return NULL;

    PyObject *ret = PyList_New(0);

    typedef diff_match_patch<typename traits::STL_STRING_TYPE> DMP;
    DMP dmp;

    PyObject *opcodes[3];
    opcodes[dmp.DELETE] = PyString_FromString("-");
    opcodes[dmp.INSERT] = PyString_FromString("+");
    opcodes[dmp.EQUAL] = PyString_FromString("=");

    typename DMP::Diffs diff;

    Py_BEGIN_ALLOW_THREADS /* RELEASE THE GIL */

    dmp.Diff_Timeout = timelimit;
    diff = dmp.diff_main(traits::to_string(a), traits::to_string(b), checklines);

    if (cleanupSemantic)
        dmp.diff_cleanupSemantic(diff);

    Py_END_ALLOW_THREADS /* ACQUIRE THE GIL */

    if (as_patch) {
        typename DMP::Patches patch = dmp.patch_make(traits::to_string(a), diff);
        typename traits::STL_STRING_TYPE patch_str = dmp.patch_toText(patch);

        return traits::from_string(patch_str);
    }

    typename std::list<typename DMP::Diff>::const_iterator entryiter;
    for (entryiter = diff.begin(); entryiter != diff.end(); entryiter++) {
        typename DMP::Diff entry = *entryiter;

        PyObject* tuple = PyTuple_New(2);

        Py_INCREF(opcodes[entry.operation]); // we're going to reuse the object, so don't let SetItem steal the reference
        PyTuple_SetItem(tuple, 0, opcodes[entry.operation]);

        if (counts_only)
            PyTuple_SetItem(tuple, 1, PyInt_FromLong(entry.text.length()));
        else
            PyTuple_SetItem(tuple, 1, traits::from_string(entry.text));

        PyList_Append(ret, tuple);
        Py_DECREF(tuple); // the list owns a reference now
    }

    // We're left with one extra reference.
    Py_DECREF(opcodes[dmp.DELETE]);
    Py_DECREF(opcodes[dmp.INSERT]);
    Py_DECREF(opcodes[dmp.EQUAL]);

    return ret;
}

template <char FMTSPEC>
static PyObject *
diff_match_patch__match__impl(PyObject *self, PyObject *args, PyObject *kwargs)
{
    typedef call_traits<FMTSPEC> traits;
    typename traits::PY_ARG_TYPE pattern, text;
    int loc;
    int match_distance = 1000;
    int match_maxbits = 32;
    float match_threshold = 0.5;
    char format_spec[64];

    static char *kwlist[] = {
        strdup("pattern"),
        strdup("text"),
        strdup("loc"),
        strdup("match_distance"),
        strdup("match_maxbits"),
        strdup("match_threshold"),
        NULL };

    sprintf(format_spec, "%c%ci|iif", FMTSPEC, FMTSPEC);
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format_spec, kwlist,
                                     &pattern, &text, &loc,
                                     &match_distance, &match_maxbits, &match_threshold)) {
        return NULL;
    }

    typedef diff_match_patch<typename traits::STL_STRING_TYPE> DMP;
    DMP dmp;

    dmp.Match_Distance = match_distance;
    dmp.Match_MaxBits = match_maxbits;
    dmp.Match_Threshold = match_threshold;

    try {
        int index = dmp.match_main(traits::to_string(pattern), traits::to_string(text), loc);
        return Py_BuildValue("i", index);
    } catch (std::exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return NULL;
    } catch (typename traits::STL_STRING_TYPE& s) {
        PyErr_SetString(PyExc_RuntimeError, traits::to_bytes(s));
        return NULL;
    }
}

// WRAPPER FUNCTIONS THAT DETERMINE WHETHER UNICODE OR BYTES ARE PASSED

static PyObject *
diff_match_patch__diff(PyObject *self, PyObject *args, PyObject *kwargs)
{
    // Check if the first argument is a Unicode object, and if so, run
    // the Unicode version of the method. Otherwise run the bytes version.
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__diff__impl<UNICODESTRING_FORMAT_SYMBOL>(self, args, kwargs);
    return diff_match_patch__diff__impl<BYTESTRING_FORMAT_SYMBOL>(self, args, kwargs);
}

static PyObject *
diff_match_patch__match(PyObject *self, PyObject *args, PyObject *kwargs)
{
    // Check if the first argument is a Unicode object, and if so, run
    // the Unicode version of the method. Otherwise run the bytes version.
    PyObject* first_arg;
    if (PyTuple_Size(args) > 0 && (first_arg = PyTuple_GetItem(args, 0)))
        if (PyUnicode_Check(first_arg))
            return diff_match_patch__match__impl<UNICODESTRING_FORMAT_SYMBOL>(self, args, kwargs);
    return diff_match_patch__match__impl<BYTESTRING_FORMAT_SYMBOL>(self, args, kwargs);
}

// EXTENSION MODULE METADATA

static PyMethodDef MyMethods[] = {
    {"diff", (PyCFunction)diff_match_patch__diff, METH_VARARGS|METH_KEYWORDS,
    "Compute the difference between two strings or bytes-like objects (Unicode and str's in Python 2). Returns a list of tuples (OP, LEN)."},
    {"match_main", (PyCFunction)diff_match_patch__match, METH_VARARGS|METH_KEYWORDS,
    "Locate the best instance of 'pattern' in 'text' near 'loc'. Returns -1 if no match found."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#if PY_MAJOR_VERSION == 2
PyMODINIT_FUNC
initfast_diff_match_patch(void)
{
    (void) Py_InitModule("fast_diff_match_patch", MyMethods);
}
#endif

#if PY_MAJOR_VERSION == 3
static struct PyModuleDef mymodule = {
   PyModuleDef_HEAD_INIT,
   "fast_diff_match_patch",   /* name of module */
   NULL, /* module documentation, may be NULL */
   -1,       /* size of per-interpreter state of the module,
                or -1 if the module keeps state in global variables. */
   MyMethods
};

PyMODINIT_FUNC
PyInit_fast_diff_match_patch(void)
{
    return PyModule_Create(&mymodule);
}
#endif

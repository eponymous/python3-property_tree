// ----------------------------------------------------------------------------
// Copyright (C) 2018 Dan Eicher
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//
// ----------------------------------------------------------------------------

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/info_parser.hpp>

typedef enum _PyPropertyTree_Flags
{
    PTREE_FLAG_NONE = 0,
    PTREE_FLAG_OBJECT_NOT_OWNED = (1 << 0),
} PyPropertyTree_Flags;

/* --- forward declarations --- */

typedef struct
{
    PyObject_HEAD
        boost::property_tree::ptree *obj;
    PyPropertyTree_Flags flags : 8;
} PyPropertyTree;

typedef struct
{
    PyObject_HEAD
        PyPropertyTree *container;
    boost::property_tree::ptree::iterator *iterator;
    PyObject *callable;
} PyPropertyTree_Iter;

typedef struct
{
    PyObject_HEAD
        PyPropertyTree *container;
    std::pair<boost::property_tree::ptree::assoc_iterator,
              boost::property_tree::ptree::assoc_iterator>
        iterator;
} PyPropertyTree_AssocIter;

extern PyTypeObject PyPropertyTree_Type;
extern PyTypeObject PyPropertyTree_IterType;
extern PyTypeObject PyPropertyTree_AssocIterType;

/* --- exceptions --- */

static PyTypeObject *PyPropertyTreeBadDataError_Type;
static PyTypeObject *PyPropertyTreeBadPathError_Type;
static PyTypeObject *PyPropertyTreeJSONParserError_Type;
static PyTypeObject *PyPropertyTreeXMLParserError_Type;
static PyTypeObject *PyPropertyTreeINIParserError_Type;
static PyTypeObject *PyPropertyTreeINFOParserError_Type;

/* --- helpers --- */

static PyPropertyTree *
PyPropertyTree_New(boost::property_tree::ptree *ptree, PyPropertyTree_Flags flag)
{
    PyPropertyTree *py_ptree;

    py_ptree = PyObject_New(PyPropertyTree, &PyPropertyTree_Type);
    py_ptree->obj = ptree;
    py_ptree->flags = flag;

    return py_ptree;
}

struct ptree_sort_helper
{
    PyObject *callable;

    ptree_sort_helper(PyObject *callable) : callable(callable) {}

    template <typename P>
    bool operator()(P &lhs, P &rhs)
    {
        PyObject *py_lhs = (PyObject *)PyPropertyTree_New(&lhs.second, PTREE_FLAG_OBJECT_NOT_OWNED);
        PyObject *py_rhs = (PyObject *)PyPropertyTree_New(&rhs.second, PTREE_FLAG_OBJECT_NOT_OWNED);

        PyObject *retval = PyObject_CallFunction(callable, (char *)"(sO)(sO)",
                                                 lhs.first.c_str(), py_lhs,
                                                 rhs.first.c_str(), py_rhs);

        Py_DECREF(py_lhs);
        Py_DECREF(py_rhs);

        if (!retval)
        {
            throw boost::property_tree::ptree_error("sort function call failed");
        }

        bool result = PyObject_IsTrue(retval);

        Py_DECREF(retval);

        return result;
    }
};

static int
py_value_to_string(PyObject *value, std::string &str)
{
    const char *value_str;
    Py_ssize_t value_len;

    if (value == Py_None)
    {
        str = "none";
    }
    else if (PyBool_Check(value))
    {
        str = (value == Py_True ? "true" : "false");
    }
    else if (PyNumber_Check(value))
    {
        PyObject *py_str = PyObject_Str(value);
        value_str = PyUnicode_AsUTF8AndSize(py_str, &value_len);
        str = std::string(value_str, value_len);
        Py_DECREF(py_str);
    }
    else if (PyUnicode_Check(value))
    {
        value_str = PyUnicode_AsUTF8AndSize(value, &value_len);
        str = std::string(value_str, value_len);
    }
    else
    {
        return -1;
    }
    return 0;
}

/* --- classes --- */

PyDoc_STRVAR(PyPropertyTree_value__doc__,
             "string value of this node\n");

static PyObject *
PyPropertyTree__get_value(PyPropertyTree *self, void *Py_UNUSED(closure))
{
    const std::string &value = self->obj->data();
    return PyUnicode_DecodeUTF8(value.c_str(), value.size(), NULL);
}

static int
PyPropertyTree__set_value(PyPropertyTree *self, PyObject *py_val, void *Py_UNUSED(closure))
{
    try
    {
        if (PyUnicode_Check(py_val))
        {
            Py_ssize_t value_len;
            const char *value = PyUnicode_AsUTF8AndSize(py_val, &value_len);
            self->obj->put_value<std::string>(std::string(value, value_len));
        }
        else
        {
            PyErr_SetObject(PyExc_ValueError, py_val);
            return -1;
        }
    }
    catch (boost::property_tree::ptree_bad_data const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeBadDataError_Type, exc.what());
        return -1;
    }
    return 0;
}

static PyGetSetDef PyPropertyTree__getsets[] = {
    {
        (char *)"value",                   /* attribute name */
        (getter)PyPropertyTree__get_value, /* C function to get the attribute */
        (setter)PyPropertyTree__set_value, /* C function to set the attribute */
        PyPropertyTree_value__doc__,       /* optional doc string */
        NULL                               /* optional additional data for getter and setter */
    },
    {NULL, NULL, NULL, NULL, NULL}};

PyDoc_STRVAR(PyPropertyTree_add__doc__,
             "add(path, value) -> Tree\n\n"
             "    Add a node at the given path with the given value.\n"
             "    If the node identified by the path does not exist, create it\n"
             "    and all its missing parents.\n"
             "    If the node already exists, add a sibling with the same key.\n");

static PyObject *
PyPropertyTree_add(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *path;
    Py_ssize_t path_len;
    PyObject *value;
    std::string value_std;
    boost::property_tree::ptree *retval;
    const char *keywords[] = {"path", "value", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O:add", (char **)keywords, &path, &path_len, &value))
    {
        return NULL;
    }

    std::string path_std(path, path_len);

    if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
    {
        retval = &self->obj->add_child(path_std, *(((PyPropertyTree *)value)->obj));
    }
    else if (py_value_to_string(value, value_std) == 0)
    {
        boost::property_tree::ptree tree(value_std);
        retval = &self->obj->add_child(path_std, tree);
    }
    else
    {
        PyErr_SetObject(PyExc_ValueError, value);
        return NULL;
    }

    return (PyObject *)PyPropertyTree_New(retval, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_append__doc__,
             "append(key, value) -> Tree\n\n"
             "    Add the value to the end of the child list with the given key.\n");

static PyObject *
PyPropertyTree_append(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *key;
    Py_ssize_t key_len;
    PyObject *value;
    std::string value_std;
    boost::property_tree::ptree::iterator retval;
    const char *keywords[] = {"key", "value", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O:append", (char **)keywords, &key, &key_len, &value))
    {
        return NULL;
    }

    if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
    {
        retval = self->obj->push_back({std::string(key, key_len), *(((PyPropertyTree *)value)->obj)});
    }
    else if (py_value_to_string(value, value_std) == 0)
    {
        boost::property_tree::ptree tree(value_std);
        retval = self->obj->push_back({std::string(key, key_len), tree});
    }
    else
    {
        PyErr_SetObject(PyExc_ValueError, value);
        return NULL;
    }

    return (PyObject *)PyPropertyTree_New(&retval->second, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_clear__doc__,
             "clear()\n\n"
             "    Clear this Tree completely of both data and children.\n");

static PyObject *
PyPropertyTree_clear(PyPropertyTree *self)
{
    self->obj->clear();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(PyPropertyTree_count__doc__,
             "count(key) -> int\n\n"
             "    Count the number of direct children with the given key.\n");

static PyObject *
PyPropertyTree_count(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *key = NULL;
    Py_ssize_t key_len;
    const char *keywords[] = {"key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#:count", (char **)keywords, &key, &key_len))
    {
        return NULL;
    }

    return PyLong_FromLong(self->obj->count(std::string(key, key_len)));
}

PyDoc_STRVAR(PyPropertyTree_empty__doc__,
             "empty() -> bool\n\n"
             "    Check whether this tree contains any children.\n");

static PyObject *
PyPropertyTree_empty(PyPropertyTree *self)
{
    return PyBool_FromLong(self->obj->empty());
}

PyDoc_STRVAR(PyPropertyTree_erase__doc__,
             "erase(key) -> int\n\n"
             "    Erase all the children with the given key and return the count.\n");

static PyObject *
PyPropertyTree_erase(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *key = NULL;
    Py_ssize_t key_len;
    const char *keywords[] = {"key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#:erase", (char **)keywords, &key, &key_len))
    {
        return NULL;
    }

    return PyLong_FromLong(self->obj->erase(std::string(key, key_len)));
}

PyDoc_STRVAR(PyPropertyTree_extend__doc__,
             "extend(iterator)\n\n"
             "    Extend the tree by appending all the items from iterator.\n");

static PyObject *
PyPropertyTree_extend(PyPropertyTree *self, PyObject *obj)
{
    PyObject *item, *iter = PyObject_GetIter(obj);

    if (iter == NULL)
    {
        return NULL;
    }

    while ((item = PyIter_Next(iter)) != NULL)
    {
        const char *key;
        Py_ssize_t key_len;
        PyObject *value;
        std::string value_std;

        if (!PyArg_ParseTuple(item, (char *)"s#O", &key, &key_len, &value))
        {
            Py_DECREF(item);
            return NULL;
        }

        if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
        {
            self->obj->push_back({std::string(key, key_len), *((PyPropertyTree *)value)->obj});
        }
        else if (py_value_to_string(value, value_std) == 0)
        {
            boost::property_tree::ptree tree(value_std);
            self->obj->push_back({std::string(key, key_len), tree});
        }
        else
        {
            PyErr_SetObject(PyExc_ValueError, value);
            Py_DECREF(item);
            return NULL;
        }

        Py_DECREF(item);
    }

    Py_DECREF(iter);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(PyPropertyTree_find__doc__,
             "find(key) -> Tree\n\n"
             "    Find a child with the given key or None.\n"
             "    * There is no guarantee about which child is returned if multiple have\n"
             "      the same key.\n");

static PyObject *
PyPropertyTree_find(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *key;
    Py_ssize_t key_len;
    const char *keywords[] = {"key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#:find", (char **)keywords, &key, &key_len))
    {
        return NULL;
    }

    boost::property_tree::ptree::assoc_iterator retval = self->obj->find(std::string(key, key_len));

    if (retval == self->obj->not_found())
        Py_RETURN_NONE;

    return (PyObject *)PyPropertyTree_New(&retval->second, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_get__doc__,
             "get(path, default=None) -> Tree\n\n"
             "    Get the child node at the given path, else default.\n"
             "    If default is not provided a BadPathError is raised.\n");

static PyObject *
PyPropertyTree_get(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    boost::property_tree::ptree *retval;
    const char *path;
    Py_ssize_t path_len;
    PyObject *py_default = NULL;
    const char *keywords[] = {"path", "default", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#|O:get", (char **)keywords, &path, &path_len, &py_default))
    {
        return NULL;
    }

    try
    {
        retval = &self->obj->get_child(std::string(path, path_len));
    }
    catch (boost::property_tree::ptree_bad_path const &exc)
    {
        if (py_default == NULL)
        {
            PyErr_SetString((PyObject *)PyPropertyTreeBadPathError_Type, exc.what());
            return NULL;
        }
        else
        {
            Py_INCREF(py_default);
            return py_default;
        }
    }

    return (PyObject *)PyPropertyTree_New(retval, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_index__doc__,
             "index(key, start=0, end=-1)\n\n"
             "    Return zero-based index in the tree of the first item\n"
             "    whose value is equal to key\n");

static PyObject *
PyPropertyTree_index(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    int start = 0;
    int end = self->obj->size();
    int index = 0;
    const char *key;
    Py_ssize_t key_len;
    const char *keywords[] = {"key", "start", "end", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#|ii:index", (char **)keywords,
                                     &key, &key_len, &start, &end))
    {
        return NULL;
    }

    std::string key_std(key, key_len);
    boost::property_tree::ptree::iterator iter(self->obj->begin());

    while (index < start)
    {
        if (iter == self->obj->end())
            goto error;
        ++index;
        ++iter;
    }

    while (index < end)
    {
        if (iter == self->obj->end())
            goto error;
        else if (iter->first == key_std)
            return PyLong_FromLong(index);
        ++index;
        ++iter;
    }

error:
    PyErr_Format(PyExc_ValueError, "%s is not in tree", key);
    return NULL;
}

PyDoc_STRVAR(PyPropertyTree_insert__doc__,
             "insert(index, key, value) -> Tree\n\n"
             "    Insert a copy of the given tree with its key\n"
             "    just before the given position in this node.\n");

static PyObject *
PyPropertyTree_insert(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    int index;
    const char *key;
    Py_ssize_t key_len;
    PyObject *value;
    std::string value_std;
    boost::property_tree::ptree::iterator retval, iter;
    const char *keywords[] = {"index", "key", "value", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"is#O:insert", (char **)keywords, &index, &key, &key_len, &value))
    {
        return NULL;
    }

    if (index < 0)
        index += self->obj->size() + 1;

    if (index < 0 || (std::size_t)index > self->obj->size())
    {
        PyErr_SetString(PyExc_IndexError, "insert index out of range");
        return NULL;
    }

    iter = self->obj->begin();

    for (int i = 0; i < index; i++)
        ++iter;

    if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
    {
        retval = self->obj->insert(iter, {std::string(key, key_len), *((PyPropertyTree *)value)->obj});
    }
    else if (py_value_to_string(value, value_std) == 0)
    {
        boost::property_tree::ptree tree(value_std);
        retval = self->obj->insert(iter, {std::string(key, key_len), tree});
    }
    else
    {
        PyErr_SetObject(PyExc_ValueError, value);
        return NULL;
    }

    return (PyObject *)PyPropertyTree_New(&retval->second, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_items__doc__,
             "items() -> iterator\n\n"
             "    Return an iterator to the ((key, value) pairs) of children.\n");

static PyObject *
PyPropertyTree_items(PyPropertyTree *self)
{
    PyPropertyTree_Iter *iter;

    iter = PyObject_GC_New(PyPropertyTree_Iter, &PyPropertyTree_IterType);
    Py_INCREF(self);
    iter->container = self;
    iter->iterator = new boost::property_tree::ptree::iterator(self->obj->begin());
    iter->callable = NULL;

    return (PyObject *)iter;
}

PyDoc_STRVAR(PyPropertyTree_keys__doc__,
             "keys()\n\n"
             "    Get a list of all the child keys.\n");

static PyObject *
PyPropertyTree_keys(PyPropertyTree *self)
{
    PyObject *list = PyList_New(self->obj->size());
    boost::property_tree::ptree::iterator iter = self->obj->begin();

    for (Py_ssize_t i = 0; iter != self->obj->end(); iter++, i++)
    {
        const std::string &key = iter->first;

        PyList_SET_ITEM(list, i, PyUnicode_DecodeUTF8(key.c_str(), key.size(), NULL));
    }

    return list;
}

PyDoc_STRVAR(PyPropertyTree_pop__doc__,
             "pop(key, default=None) -> Tree\n\n"
             "    Remove the child with the given key and return its value, else default.\n"
             "    If default is not given and key is not in the tree, a KeyError is raised.\n"
             "    * There is no guarantee about which child is returned if multiple have\n"
             "      the same key.\n");

static PyObject *
PyPropertyTree_pop(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *key = NULL;
    Py_ssize_t key_len;
    PyObject *py_default = NULL;
    PyPropertyTree *py_ptree;
    const char *keywords[] = {"index", "default", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#|O:pop", (char **)keywords, &key, &key_len, &py_default))
    {
        return NULL;
    }

    boost::property_tree::ptree::assoc_iterator iter(self->obj->find(std::string(key, key_len)));

    if (iter == self->obj->not_found())
    {
        if (py_default == NULL)
        {
            PyErr_SetString(PyExc_KeyError, key);
            return NULL;
        }
        else
        {
            Py_INCREF(py_default);
            return py_default;
        }
    }

    py_ptree = PyPropertyTree_New(new boost::property_tree::ptree(iter->second), PTREE_FLAG_NONE);

    self->obj->erase(self->obj->to_iterator(iter));

    return (PyObject *)py_ptree;
}

PyDoc_STRVAR(PyPropertyTree_popitem__doc__,
             "popitem(index=-1) -> (key, value)\n\n"
             "    Remove and return the child at the given index.\n"
             "    If no index is specified remove and return the last child.\n");

static PyObject *
PyPropertyTree_popitem(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    int index = self->obj->size() - 1;
    PyPropertyTree *py_ptree;
    const char *keywords[] = {"index", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"|i:popitem", (char **)keywords, &index))
    {
        return NULL;
    }

    if (index < 0)
        index += self->obj->size();

    if (index < 0 || (std::size_t)index >= self->obj->size())
    {
        PyErr_SetString(PyExc_IndexError, "popitem index out of range");
        return NULL;
    }

    boost::property_tree::ptree::iterator iter(self->obj->begin());

    for (int i = 0; i < index; i++)
        ++iter;

    std::string key = iter->first;
    py_ptree = PyPropertyTree_New(new boost::property_tree::ptree(iter->second), PTREE_FLAG_NONE);

    self->obj->erase(iter);

    return Py_BuildValue((char *)"s#N", key.c_str(), key.size(), py_ptree);
}

PyDoc_STRVAR(PyPropertyTree_put__doc__,
             "put(path, value) -> Tree\n\n"
             "    Set the node at the given path to the given value.\n"
             "    If the node identified by the path does not exist, create it and\n"
             "    all its missing parents.\n"
             "    If the node at the path already exists, replace its value.\n");

static PyObject *
PyPropertyTree_put(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *path;
    Py_ssize_t path_len;
    PyObject *value;
    std::string value_std;
    boost::property_tree::ptree *retval;
    const char *keywords[] = {"path", "value", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O:put", (char **)keywords, &path, &path_len, &value))
    {
        return NULL;
    }

    std::string path_std(path, path_len);

    if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
    {
        retval = &self->obj->put_child(path_std, *(((PyPropertyTree *)value)->obj));
    }
    else if (py_value_to_string(value, value_std) == 0)
    {
        boost::property_tree::ptree tree(value_std);
        retval = &self->obj->put_child(path_std, tree);
    }
    else
    {
        PyErr_SetObject(PyExc_ValueError, value);
        return NULL;
    }

    return (PyObject *)PyPropertyTree_New(retval, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_setdefault__doc__,
             "setdefault(path, default=None) -> Tree\n\n"
             "    If path is in the tree, return its value.\n"
             "    If the node identified by the path does not exist, create it and\n"
             "    all its missing parents.\n");

PyDoc_STRVAR(PyPropertyTree_remove__doc__,
             "remove(key)\n\n"
             "    Remove the first child whose value is equal to key.\n");

static PyObject *
PyPropertyTree_remove(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *key;
    Py_ssize_t key_len;
    const char *keywords[] = {"key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#:remove", (char **)keywords, &key, &key_len))
    {
        return NULL;
    }

    std::string key_std(key, key_len);

    for (boost::property_tree::ptree::iterator iter = self->obj->begin(); iter != self->obj->end(); iter++)
    {
        if (iter->first == key_std)
        {
            self->obj->erase(iter);
            Py_RETURN_NONE;
        }
    }

    PyErr_SetNone(PyExc_ValueError);
    return NULL;
}

PyDoc_STRVAR(PyPropertyTree_reverse__doc__,
             "reverse()\n\n"
             "    Reverse the children in place.\n");

static PyObject *
PyPropertyTree_reverse(PyPropertyTree *self)
{
    self->obj->reverse();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(PyPropertyTree_search__doc__,
             "search(arg) -> iterator\n\n"
             "    Return an iterator to the children that match this key.\n"
             "    Argument can be a string or function of the type: func(key, value) -> Bool\n");

static PyObject *
PyPropertyTree_search(PyPropertyTree *self, PyObject *arg)
{
    const char *key;
    Py_ssize_t key_len;

    if (PyUnicode_Check(arg))
    {
        PyPropertyTree_AssocIter *iter;

        key = PyUnicode_AsUTF8AndSize(arg, &key_len);
        iter = PyObject_GC_New(PyPropertyTree_AssocIter, &PyPropertyTree_AssocIterType);

        Py_INCREF(self);

        iter->container = self;
        iter->iterator = self->obj->equal_range(std::string(key, key_len));

        return (PyObject *)iter;
    }
    else if (PyCallable_Check(arg))
    {
        PyPropertyTree_Iter *iter;
        iter = PyObject_GC_New(PyPropertyTree_Iter, &PyPropertyTree_IterType);

        Py_INCREF(self);
        Py_XINCREF(arg);

        iter->container = self;
        iter->iterator = new boost::property_tree::ptree::iterator(self->obj->begin());
        iter->callable = arg;

        return (PyObject *)iter;
    }

    PyErr_SetString(PyExc_TypeError, "argument not a string or callable object");
    return NULL;
}

static PyObject *
PyPropertyTree_setdefault(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    const char *path;
    Py_ssize_t path_len;
    PyObject *value = Py_None;
    std::string value_std;
    boost::property_tree::ptree *retval;
    const char *keywords[] = {"path", "default", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#|O:setdefault", (char **)keywords, &path, &path_len, &value))
    {
        return NULL;
    }

    std::string path_std(path, path_len);

    try
    {
        retval = &self->obj->get_child(path_std);
    }
    catch (boost::property_tree::ptree_bad_path const &exc)
    {
        if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
        {
            retval = &self->obj->put_child(path_std, *(((PyPropertyTree *)value)->obj));
        }
        else if (py_value_to_string(value, value_std) == 0)
        {
            boost::property_tree::ptree tree(value_std);
            retval = &self->obj->put_child(path_std, tree);
        }
        else
        {
            PyErr_SetObject(PyExc_ValueError, value);
            return NULL;
        }
    }

    return (PyObject *)PyPropertyTree_New(retval, PTREE_FLAG_OBJECT_NOT_OWNED);
}

PyDoc_STRVAR(PyPropertyTree_sort__doc__,
             "sort(key=None)\n\n"
             "    Sort the children according to key order or a callable object.\n");

static PyObject *
PyPropertyTree_sort(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    PyObject *callable = NULL;
    const char *keywords[] = {"key", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"|O:sort", (char **)keywords, &callable))
    {
        return NULL;
    }

    if (!callable)
    {
        self->obj->sort();
        Py_RETURN_NONE;
    }

    if (!PyCallable_Check(callable))
    {
        PyErr_SetString(PyExc_TypeError, "object not callable");
        return NULL;
    }

    try
    {
        Py_INCREF(callable);

        self->obj->sort(ptree_sort_helper(callable));

        Py_DECREF(callable);

        Py_RETURN_NONE;
    }
    catch (boost::property_tree::ptree_error const &exc)
    {
        /* if python threw an error use that */
        if (!PyErr_Occurred())
            PyErr_SetString((PyObject *)PyExc_RuntimeError, exc.what());

        return NULL;
    }
}

PyDoc_STRVAR(PyPropertyTree_sorted__doc__,
             "sorted() -> iterator\n\n"
             "    Get an iterator to the sorted children of this node, in key order.\n");

static PyObject *
PyPropertyTree_sorted(PyPropertyTree *self)
{
    PyPropertyTree_AssocIter *iter;

    iter = PyObject_GC_New(PyPropertyTree_AssocIter, &PyPropertyTree_AssocIterType);
    Py_INCREF(self);
    iter->container = self;
    iter->iterator = {self->obj->ordered_begin(), self->obj->not_found()};

    return (PyObject *)iter;
}

PyDoc_STRVAR(PyPropertyTree_values__doc__,
             "values()\n\n"
             "    Get a list of the children values.\n");

static PyObject *
PyPropertyTree_values(PyPropertyTree *self)
{
    PyObject *list = PyList_New(self->obj->size());
    boost::property_tree::ptree::iterator iter = self->obj->begin();

    for (Py_ssize_t i = 0; iter != self->obj->end(); iter++, i++)
    {
        PyList_SET_ITEM(list, i, (PyObject *)PyPropertyTree_New(&iter->second, PTREE_FLAG_OBJECT_NOT_OWNED));
    }

    return list;
}

static PyObject *
PyPropertyTree__copy__(PyPropertyTree *self)
{
    return (PyObject *)PyPropertyTree_New(new boost::property_tree::ptree(*self->obj), PTREE_FLAG_NONE);
}

static PyObject *
PyPropertyTree__reduce__(PyPropertyTree *self)
{
    PyObject *args, *py_iter;

    if (self->obj->data() == "")
    {
        args = PyTuple_New(0);
    }
    else
    {
        args = PyTuple_New(1);
        PyTuple_SET_ITEM(args, 0, PyUnicode_DecodeUTF8(self->obj->data().c_str(), self->obj->data().size(), NULL));
    }

    if (self->obj->begin() == self->obj->end())
    {
        Py_INCREF(Py_None);
        py_iter = Py_None;
    }
    else
    {
        PyPropertyTree_Iter *iter = PyObject_GC_New(PyPropertyTree_Iter, &PyPropertyTree_IterType);
        Py_INCREF(self);
        iter->container = self;
        iter->iterator = new boost::property_tree::ptree::iterator(self->obj->begin());
        iter->callable = NULL;
        py_iter = (PyObject *)iter;
    }

    return Py_BuildValue("ONOON", (PyObject *)&PyPropertyTree_Type, args, Py_None, Py_None, py_iter);
}

static PyMethodDef PyPropertyTree_methods[] = {
    {(char *)"add",
     (PyCFunction)PyPropertyTree_add,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_add__doc__},
    {(char *)"append",
     (PyCFunction)PyPropertyTree_append,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_append__doc__},
    {(char *)"clear",
     (PyCFunction)PyPropertyTree_clear,
     METH_NOARGS,
     PyPropertyTree_clear__doc__},
    {(char *)"count",
     (PyCFunction)PyPropertyTree_count,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_count__doc__},
    {(char *)"empty",
     (PyCFunction)PyPropertyTree_empty,
     METH_NOARGS,
     PyPropertyTree_empty__doc__},
    {(char *)"erase",
     (PyCFunction)PyPropertyTree_erase,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_erase__doc__},
    {(char *)"extend",
     (PyCFunction)PyPropertyTree_extend,
     METH_O,
     PyPropertyTree_extend__doc__},
    {(char *)"find",
     (PyCFunction)PyPropertyTree_find,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_find__doc__},
    {(char *)"get",
     (PyCFunction)PyPropertyTree_get,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_get__doc__},
    {(char *)"index",
     (PyCFunction)PyPropertyTree_index,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_index__doc__},
    {(char *)"insert",
     (PyCFunction)PyPropertyTree_insert,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_insert__doc__},
    {(char *)"items",
     (PyCFunction)PyPropertyTree_items,
     METH_NOARGS,
     PyPropertyTree_items__doc__},
    {(char *)"keys",
     (PyCFunction)PyPropertyTree_keys,
     METH_NOARGS,
     PyPropertyTree_keys__doc__},
    {(char *)"pop",
     (PyCFunction)PyPropertyTree_pop,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_pop__doc__},
    {(char *)"popitem",
     (PyCFunction)PyPropertyTree_popitem,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_popitem__doc__},
    {(char *)"put",
     (PyCFunction)PyPropertyTree_put,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_put__doc__},
    {(char *)"remove",
     (PyCFunction)PyPropertyTree_remove,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_remove__doc__},
    {(char *)"reverse",
     (PyCFunction)PyPropertyTree_reverse,
     METH_NOARGS,
     PyPropertyTree_reverse__doc__},
    {(char *)"search",
     (PyCFunction)PyPropertyTree_search,
     METH_O,
     PyPropertyTree_search__doc__},
    {(char *)"setdefault",
     (PyCFunction)PyPropertyTree_setdefault,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_setdefault__doc__},
    {(char *)"sort",
     (PyCFunction)PyPropertyTree_sort,
     METH_KEYWORDS | METH_VARARGS,
     PyPropertyTree_sort__doc__},
    {(char *)"sorted",
     (PyCFunction)PyPropertyTree_sorted,
     METH_NOARGS,
     PyPropertyTree_sorted__doc__},
    {(char *)"values",
     (PyCFunction)PyPropertyTree_values,
     METH_NOARGS,
     PyPropertyTree_values__doc__},
    {(char *)"__copy__",
     (PyCFunction)PyPropertyTree__copy__,
     METH_NOARGS,
     NULL},
    {(char *)"__reduce__",
     (PyCFunction)PyPropertyTree__reduce__,
     METH_NOARGS,
     NULL},
    {NULL, NULL, 0, NULL}};

static PyObject *
PyPropertyTree__tp_richcompare(PyPropertyTree *self, PyObject *other, int op)
{
    try
    {
        if (PyObject_IsInstance(other, (PyObject *)&PyPropertyTree_Type))
        {
            switch (op)
            {
            case Py_EQ:
                if (*self->obj == *((PyPropertyTree *)other)->obj)
                    Py_RETURN_TRUE;
                break;
            case Py_NE:
                if (*self->obj != *((PyPropertyTree *)other)->obj)
                    Py_RETURN_TRUE;
                break;
            default:
                Py_RETURN_NOTIMPLEMENTED;
            }
        }
        else if (other == Py_None)
        {
            switch (op)
            {
            case Py_EQ:
                if (self->obj->data() == "none")
                    Py_RETURN_TRUE;
                break;
            case Py_NE:
                if (self->obj->data() != "none")
                    Py_RETURN_TRUE;
                break;
            default:
                Py_RETURN_NOTIMPLEMENTED;
            }
        }
        else if (PyBool_Check(other))
        {
            bool value = (other == Py_True ? true : false);
            switch (op)
            {
            case Py_LT:
                if (self->obj->get_value<bool>() < value)
                    Py_RETURN_TRUE;
                break;
            case Py_LE:
                if (self->obj->get_value<bool>() <= value)
                    Py_RETURN_TRUE;
                break;
            case Py_EQ:
                if (self->obj->get_value<bool>() == value)
                    Py_RETURN_TRUE;
                break;
            case Py_NE:
                if (self->obj->get_value<bool>() != value)
                    Py_RETURN_TRUE;
                break;
            case Py_GE:
                if (self->obj->get_value<bool>() >= value)
                    Py_RETURN_TRUE;
                break;
            case Py_GT:
                if (self->obj->get_value<bool>() > value)
                    Py_RETURN_TRUE;
                break;
            }
        }
        else if (PyLong_Check(other))
        {
            long value = PyLong_AsLong(other);
            switch (op)
            {
            case Py_LT:
                if (self->obj->get_value<long>() < value)
                    Py_RETURN_TRUE;
                break;
            case Py_LE:
                if (self->obj->get_value<long>() <= value)
                    Py_RETURN_TRUE;
                break;
            case Py_EQ:
                if (self->obj->get_value<long>() == value)
                    Py_RETURN_TRUE;
                break;
            case Py_NE:
                if (self->obj->get_value<long>() != value)
                    Py_RETURN_TRUE;
                break;
            case Py_GE:
                if (self->obj->get_value<long>() >= value)
                    Py_RETURN_TRUE;
                break;
            case Py_GT:
                if (self->obj->get_value<long>() > value)
                    Py_RETURN_TRUE;
                break;
            }
        }
        else if (PyFloat_Check(other))
        {
            double value = PyFloat_AsDouble(other);
            switch (op)
            {
            case Py_LT:
                if (self->obj->get_value<double>() < value)
                    Py_RETURN_TRUE;
                break;
            case Py_LE:
                if (self->obj->get_value<double>() <= value)
                    Py_RETURN_TRUE;
                break;
            case Py_EQ:
                if (self->obj->get_value<double>() == value)
                    Py_RETURN_TRUE;
                break;
            case Py_NE:
                if (self->obj->get_value<double>() != value)
                    Py_RETURN_TRUE;
                break;
            case Py_GE:
                if (self->obj->get_value<double>() >= value)
                    Py_RETURN_TRUE;
                break;
            case Py_GT:
                if (self->obj->get_value<double>() > value)
                    Py_RETURN_TRUE;
                break;
            }
        }
        else if (PyUnicode_Check(other))
        {
            Py_ssize_t value_len;
            const char *value = PyUnicode_AsUTF8AndSize(other, &value_len);
            switch (op)
            {
            case Py_LT:
                if (self->obj->data() < std::string(value, value_len))
                    Py_RETURN_TRUE;
                break;
            case Py_LE:
                if (self->obj->data() <= std::string(value, value_len))
                    Py_RETURN_TRUE;
                break;
            case Py_EQ:
                if (self->obj->data() == std::string(value, value_len))
                    Py_RETURN_TRUE;
                break;
            case Py_NE:
                if (self->obj->data() != std::string(value, value_len))
                    Py_RETURN_TRUE;
                break;
            case Py_GE:
                if (self->obj->data() >= std::string(value, value_len))
                    Py_RETURN_TRUE;
                break;
            case Py_GT:
                if (self->obj->data() > std::string(value, value_len))
                    Py_RETURN_TRUE;
                break;
            }
        }
    }
    catch (boost::property_tree::ptree_bad_data const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeBadDataError_Type, exc.what());
        return NULL;
    }

    Py_RETURN_FALSE;
}

static int
PyPropertyTree__nb_bool(PyPropertyTree *self)
{
    try
    {
        return self->obj->get_value<bool>();
    }
    catch (boost::property_tree::ptree_bad_data const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeBadDataError_Type, exc.what());
        return -1;
    }
}

static PyObject *
PyPropertyTree__nb_int(PyPropertyTree *self)
{
    try
    {
        return PyLong_FromLong(self->obj->get_value<int>());
    }
    catch (boost::property_tree::ptree_bad_data const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeBadDataError_Type, exc.what());
        return NULL;
    }
}

static PyObject *
PyPropertyTree__nb_float(PyPropertyTree *self)
{
    try
    {
        return PyFloat_FromDouble(self->obj->get_value<double>());
    }
    catch (boost::property_tree::ptree_bad_data const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeBadDataError_Type, exc.what());
        return NULL;
    }
}

static PyObject *
PyPropertyTree__nb_or(PyObject *py_left, PyObject *py_right)
{
    PyPropertyTree *left;
    PyPropertyTree *right;
    PyPropertyTree *retval;

    if (!PyObject_IsInstance(py_left, (PyObject *)&PyPropertyTree_Type) ||
        !PyObject_IsInstance(py_right, (PyObject *)&PyPropertyTree_Type))
    {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    left = (PyPropertyTree *)py_left;
    right = (PyPropertyTree *)py_right;

    retval = PyPropertyTree_New(new boost::property_tree::ptree(*left->obj), PTREE_FLAG_NONE);

    for (boost::property_tree::ptree::iterator iter = right->obj->begin(); iter != right->obj->end(); iter++)
    {
        retval->obj->put_child(iter->first, iter->second);
    }

    return (PyObject *)retval;
}

static PyObject *
PyPropertyTree__nb_inplace_or(PyPropertyTree *self, PyObject *py_right)
{
    PyPropertyTree *right;

    if (!PyObject_IsInstance(py_right, (PyObject *)&PyPropertyTree_Type))
    {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    right = (PyPropertyTree *)py_right;

    for (boost::property_tree::ptree::iterator iter = right->obj->begin(); iter != right->obj->end(); iter++)
    {
        self->obj->put_child(iter->first, iter->second);
    }

    Py_INCREF((PyObject *)self);
    return (PyObject *)self;
}

static PyNumberMethods PyPropertyTree__tp_as_number = {
    (binaryfunc)NULL,                          /* nb_add */
    (binaryfunc)NULL,                          /* nb_subtract */
    (binaryfunc)NULL,                          /* nb_multiply */
    (binaryfunc)NULL,                          /* nb_remainder */
    (binaryfunc)NULL,                          /* nb_divmod */
    (ternaryfunc)NULL,                         /* nb_power */
    (unaryfunc)NULL,                           /* nb_negative */
    (unaryfunc)NULL,                           /* nb_positive */
    (unaryfunc)NULL,                           /* nb_absolute */
    (inquiry)PyPropertyTree__nb_bool,          /* nb_bool */
    (unaryfunc)NULL,                           /* nb_invert */
    (binaryfunc)NULL,                          /* nb_lshift */
    (binaryfunc)NULL,                          /* nb_rshift */
    (binaryfunc)NULL,                          /* nb_and */
    (binaryfunc)NULL,                          /* nb_xor */
    (binaryfunc)PyPropertyTree__nb_or,         /* nb_or */
    (unaryfunc)PyPropertyTree__nb_int,         /* nb_int */
    (void *)NULL,                              /* nb_reserved */
    (unaryfunc)PyPropertyTree__nb_float,       /* nb_float */
    (binaryfunc)NULL,                          /* nb_inplace_add */
    (binaryfunc)NULL,                          /* nb_inplace_subtract */
    (binaryfunc)NULL,                          /* nb_inplace_multiply */
    (binaryfunc)NULL,                          /* nb_inplace_remainder */
    (ternaryfunc)NULL,                         /* nb_inplace_power */
    (binaryfunc)NULL,                          /* nb_inplace_lshift */
    (binaryfunc)NULL,                          /* nb_inplace_rshift */
    (binaryfunc)NULL,                          /* nb_inplace_and */
    (binaryfunc)NULL,                          /* nb_inplace_xor */
    (binaryfunc)PyPropertyTree__nb_inplace_or, /* nb_inplace_or */
    (binaryfunc)NULL,                          /* nb_floor_divide */
    (binaryfunc)NULL,                          /* nb_true_divide */
    (binaryfunc)NULL,                          /* nb_inplace_floor_divide */
    (binaryfunc)NULL,                          /* nb_inplace_true_divide */
    (unaryfunc)NULL,                           /* nb_index */
    (binaryfunc)NULL,                          /* nb_matrix_multiply */
    (binaryfunc)NULL,                          /* nb_inplace_matrix_multiply */
};

static Py_ssize_t
PyPropertyTree_mp_length(PyObject *self)
{
    return ((PyPropertyTree *)self)->obj->size();
}

static PyObject *
PyPropertyTree_mp_subscript(PyObject *self, PyObject *key)
{
    const char *path;
    Py_ssize_t path_len;
    boost::property_tree::ptree *retval;
    boost::property_tree::ptree *tree = ((PyPropertyTree *)self)->obj;

    if (PyIndex_Check(key))
    {
        int index = PyLong_AsSsize_t(key);

        if (index < 0)
            index += (Py_ssize_t)tree->size();

        if (index >= 0 && index < (Py_ssize_t)tree->size())
        {
            boost::property_tree::ptree::iterator iter(tree->begin());

            for (Py_ssize_t i = 0; i < index; i++)
                ++iter;

            return (PyObject *)PyPropertyTree_New(&iter->second, PTREE_FLAG_OBJECT_NOT_OWNED);
        }

        PyErr_Format(PyExc_IndexError, "%s index out of range", Py_TYPE(self)->tp_name);
        return NULL;
    }
    else if (!PyUnicode_Check(key))
    {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }

    path = PyUnicode_AsUTF8AndSize(key, &path_len);

    try
    {
        retval = &tree->get_child(std::string(path, path_len));
    }
    catch (boost::property_tree::ptree_bad_path const &exc)
    {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }

    return (PyObject *)PyPropertyTree_New(retval, PTREE_FLAG_OBJECT_NOT_OWNED);
}

static int
PyPropertyTree_mp_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    std::string path_std;
    std::string value_std;
    boost::property_tree::ptree *tree = ((PyPropertyTree *)self)->obj;

    if (PyIndex_Check(key))
    {
        int index = PyLong_AsSsize_t(key);

        if (index < 0)
            index += (Py_ssize_t)tree->size();

        if (index >= 0 && index < (Py_ssize_t)tree->size())
        {
            boost::property_tree::ptree::iterator iter(tree->begin());

            for (Py_ssize_t i = 0; i < index; i++)
                ++iter;

            path_std = iter->first;
        }
        else
        {
            PyErr_Format(PyExc_IndexError, "%s index out of range", Py_TYPE(self)->tp_name);
            return -1;
        }
    }
    else if (PyUnicode_Check(key))
    {
        Py_ssize_t path_len;
        const char *path = PyUnicode_AsUTF8AndSize(key, &path_len);
        path_std = std::string(path, path_len);
    }
    else
    {
        PyErr_SetObject(PyExc_KeyError, key);
        return -1;
    }

    if (value == NULL)
    {
        for (boost::property_tree::ptree::iterator iter = tree->begin(); iter != tree->end(); iter++)
        {
            if (iter->first == path_std)
            {
                tree->erase(iter);
                return 0;
            }
        }
        PyErr_SetObject(PyExc_KeyError, key);
        return -1;
    }
    else
    {
        if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
        {
            tree->put_child(path_std, *(((PyPropertyTree *)value)->obj));
        }
        else if (py_value_to_string(value, value_std) == 0)
        {
            boost::property_tree::ptree value_tree(value_std);
            tree->put_child(path_std, value_tree);
        }
        else
        {
            PyErr_SetObject(PyExc_ValueError, value);
            return -1;
        }
    }

    return 0;
}

static PyMappingMethods PyPropertyTree_as_mapping = {
    PyPropertyTree_mp_length,
    PyPropertyTree_mp_subscript,
    PyPropertyTree_mp_ass_subscript,
};

static int
PyPropertyTree__sq_contains(PyPropertyTree *self, PyObject *py_value)
{
    if (PyUnicode_Check(py_value))
    {
        Py_ssize_t value_len;
        const char *value = PyUnicode_AsUTF8AndSize(py_value, &value_len);

        if (value == NULL || PyErr_Occurred())
        {
            PyErr_Clear();
            return 0;
        }

        std::string str(value, value_len);

        // check the keys first then check the data string
        return (self->obj->find(str) != self->obj->not_found()) ||
               (self->obj->data().find(str) != std::string::npos);
    }
    else if (PyObject_IsInstance(py_value, (PyObject *)&PyPropertyTree_Type))
    {
        const std::string &value = ((PyPropertyTree *)py_value)->obj->data();

        return (self->obj->find(value) != self->obj->not_found());
    }

    return 0;
}

PyObject *
PyPropertyTree__sq_inplace_concat(PyPropertyTree *self, PyObject *py_value)
{
    if (PyObject_IsInstance(py_value, (PyObject *)&PyPropertyTree_Type))
    {
        PyPropertyTree *value = (PyPropertyTree *)py_value;

        self->obj->insert(self->obj->end(), value->obj->begin(), value->obj->end());

        Py_INCREF(self);

        return (PyObject *)self;
    }

    PyErr_Format(PyExc_TypeError, "unsupported operand type(s) for +=: '%s' and '%s'",
                 Py_TYPE(self)->tp_name, Py_TYPE(py_value)->tp_name);
    return NULL;
}

static PySequenceMethods PyPropertyTree__tp_as_sequence = {
    (lenfunc)NULL,      /* sq_length */
    (binaryfunc)NULL,   /* sq_concat */
    (ssizeargfunc)NULL, /* sq_repeat */
    (ssizeargfunc)NULL, /* sq_item */
    NULL,
    (ssizeobjargproc)NULL, /* sq_ass_item */
    NULL,
    (objobjproc)PyPropertyTree__sq_contains,       /* sq_contains */
    (binaryfunc)PyPropertyTree__sq_inplace_concat, /* sq_inplace_concat */
    (ssizeargfunc)NULL,                            /* sq_inplace_repeat */
};

static PyObject *
PyPropertyTree__tp_iter(PyPropertyTree *self)
{
    PyPropertyTree_Iter *iter;

    iter = PyObject_GC_New(PyPropertyTree_Iter, &PyPropertyTree_IterType);
    Py_INCREF(self);
    iter->container = self;
    iter->iterator = new boost::property_tree::ptree::iterator(self->obj->begin());
    iter->callable = NULL;

    return (PyObject *)iter;
}

static PyObject *
PyPropertyTree__tp_str(PyPropertyTree *self)
{
    return PyUnicode_DecodeUTF8(self->obj->data().c_str(), self->obj->data().size(), NULL);
}

static int
PyPropertyTree__tp_init(PyPropertyTree *self, PyObject *args, PyObject *kwargs)
{
    PyObject *key, *value = NULL;
    std::string value_std;

    // dict style keyword constructor
    // t = Tree(a=1, b=2, c=3)
    // len(t)                        -> 3
    // t.keys()                      -> ['a', 'b', 'c']
    // [v.value for v in t.values()] -> ['1', '2', '3']
    if (kwargs != NULL && PyTuple_GET_SIZE(args) == 0 && PyArg_ValidateKeywordArguments(kwargs))
    {
        Py_ssize_t pos = 0;
        self->obj = new boost::property_tree::ptree();

        while (PyDict_Next(kwargs, &pos, &key, &value))
        {
            Py_ssize_t key_len;
            const char *c_key = PyUnicode_AsUTF8AndSize(key, &key_len);

            if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
            {
                self->obj->push_back({std::string(c_key, key_len), *((PyPropertyTree *)value)->obj});
            }
            else if (py_value_to_string(value, value_std) == 0)
            {
                boost::property_tree::ptree tree(value_std);
                self->obj->push_back({std::string(c_key, key_len), tree});
            }
            else
            {
                PyErr_SetObject(PyExc_ValueError, value);
                return -1;
            }
        }
        // regular t = Tree("foo") value constructor
    }
    else if (PyArg_ParseTuple(args, (char *)"|O", &value))
    {
        if (kwargs != NULL)
        {
            PyErr_Format(PyExc_TypeError, "Tree() doesn't support mixed keyword/value args");
            return -1;
        }
        if (value)
        {
            if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
            {
                self->obj = new boost::property_tree::ptree(*((PyPropertyTree *)value)->obj);
            }
            else if (py_value_to_string(value, value_std) == 0)
            {
                self->obj = new boost::property_tree::ptree(value_std);
            }
            else
            {
                PyErr_Format(PyExc_TypeError, "unsupported argument type: '%s'", Py_TYPE(value)->tp_name);
                return -1;
            }
        }
        else
        {
            self->obj = new boost::property_tree::ptree();
        }
    }
    else
    {
        return -1;
    }

    self->flags = PTREE_FLAG_NONE;
    return 0;
}

static void
PyPropertyTree__tp_dealloc(PyPropertyTree *self)
{
    boost::property_tree::ptree *tmp = self->obj;
    self->obj = NULL;
    if (!(self->flags & PTREE_FLAG_OBJECT_NOT_OWNED))
    {
        delete tmp;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
PyPropertyTree__tp_getattro(PyPropertyTree *self, PyObject *name)
{
    // do normal lookup first so we don't break stuff like function calls
    PyObject *attr = PyObject_GenericGetAttr((PyObject *)self, name);

    if (attr)
        return attr;

    // PyObject_GenericGetAttr() didn't find anything look, in the children
    if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_AttributeError))
    {

        boost::property_tree::ptree *retval;
        Py_ssize_t key_len;
        const char *key = PyUnicode_AsUTF8AndSize(name, &key_len); // assume python already checked the type

        try
        {
            retval = &self->obj->get_child(std::string(key, key_len));
        }
        catch (boost::property_tree::ptree_bad_path const &exc)
        {
            /* rethrow AttributeError */
            return NULL;
        }

        PyErr_Clear(); // found a child, clear the exception

        return (PyObject *)PyPropertyTree_New(retval, PTREE_FLAG_OBJECT_NOT_OWNED);
    }

    /* keep whatever exception python threw */
    return NULL;
}

static int
PyPropertyTree__tp_setattro(PyPropertyTree *self, PyObject *name, PyObject *value)
{
    // do normal lookup first -- might not even be needed...
    if (PyObject_GenericSetAttr((PyObject *)self, name, value) == 0)
        return 0;

    if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_AttributeError))
    {
        Py_ssize_t key_len;
        const char *key = PyUnicode_AsUTF8AndSize(name, &key_len);
        std::string value_std, key_std(key, key_len);

        if (PyObject_IsInstance(value, (PyObject *)&PyPropertyTree_Type))
        {
            self->obj->put_child(key_std, *(((PyPropertyTree *)value)->obj));
        }
        else if (py_value_to_string(value, value_std) == 0)
        {
            boost::property_tree::ptree tree(value_std);
            self->obj->put_child(key_std, tree);
        }
        else
        {
            PyErr_Clear();
            PyErr_SetObject(PyExc_ValueError, value);
            return -1;
        }
        PyErr_Clear();
        return 0;
    }

    /* keep whatever exception python threw */
    return -1;
}

PyDoc_STRVAR(PyPropertyTree__doc__,
             "    Property tree main structure.\n"
             "    A property tree is a hierarchical data structure which has one data element\n"
             "    in each node as well as an ordered sequence of sub-nodes which are\n"
             "    additionally identified by a non-unique key.\n");

PyTypeObject PyPropertyTree_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)(char *) "proptree.Tree", /* tp_name */
    sizeof(PyPropertyTree),                                 /* tp_basicsize */
    0,                                                      /* tp_itemsize */
    (destructor)PyPropertyTree__tp_dealloc,                 /* tp_dealloc */
    (printfunc)0,                                           /* tp_print */
    (getattrfunc)NULL,                                      /* tp_getattr */
    (setattrfunc)NULL,                                      /* tp_setattr */
    (PyAsyncMethods *)NULL,                                 /* tp_compare */
    (reprfunc)NULL,                                         /* tp_repr */
    (PyNumberMethods *)&PyPropertyTree__tp_as_number,       /* tp_as_number */
    (PySequenceMethods *)&PyPropertyTree__tp_as_sequence,   /* tp_as_sequence */
    (PyMappingMethods *)&PyPropertyTree_as_mapping,         /* tp_as_mapping */
    (hashfunc)NULL,                                         /* tp_hash */
    (ternaryfunc)NULL,                                      /* tp_call */
    (reprfunc)PyPropertyTree__tp_str,                       /* tp_str */
    (getattrofunc)PyPropertyTree__tp_getattro,              /* tp_getattro */
    (setattrofunc)PyPropertyTree__tp_setattro,              /* tp_setattro */
    (PyBufferProcs *)NULL,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                                     /* tp_flags */
    PyPropertyTree__doc__,                                  /* Documentation string */
    (traverseproc)NULL,                                     /* tp_traverse */
    (inquiry)NULL,                                          /* tp_clear */
    (richcmpfunc)PyPropertyTree__tp_richcompare,            /* tp_richcompare */
    0,                                                      /* tp_weaklistoffset */
    (getiterfunc)PyPropertyTree__tp_iter,                   /* tp_iter */
    (iternextfunc)NULL,                                     /* tp_iternext */
    (struct PyMethodDef *)PyPropertyTree_methods,           /* tp_methods */
    (struct PyMemberDef *)0,                                /* tp_members */
    PyPropertyTree__getsets,                                /* tp_getset */
    NULL,                                                   /* tp_base */
    NULL,                                                   /* tp_dict */
    (descrgetfunc)NULL,                                     /* tp_descr_get */
    (descrsetfunc)NULL,                                     /* tp_descr_set */
    0,                                                      /* tp_dictoffset */
    (initproc)PyPropertyTree__tp_init,                      /* tp_init */
    (allocfunc)PyType_GenericAlloc,                         /* tp_alloc */
    (newfunc)PyType_GenericNew,                             /* tp_new */
    (freefunc)0,                                            /* tp_free */
    (inquiry)NULL,                                          /* tp_is_gc */
    NULL,                                                   /* tp_bases */
    NULL,                                                   /* tp_mro */
    NULL,                                                   /* tp_cache */
    NULL,                                                   /* tp_subclasses */
    NULL,                                                   /* tp_weaklist */
    (destructor)NULL                                        /* tp_del */
};

static void
PyPropertyTree_Iter__tp_clear(PyPropertyTree_Iter *self)
{
    Py_CLEAR(self->container);
    delete self->iterator;
    self->iterator = NULL;
}

static int
PyPropertyTree_Iter__tp_traverse(PyPropertyTree_Iter *self, visitproc visit, void *arg)
{
    Py_VISIT((PyObject *)self->container);
    return 0;
}

static void
PyPropertyTree_Iter__tp_dealloc(PyPropertyTree_Iter *self)
{
    Py_CLEAR(self->container);
    delete self->iterator;
    self->iterator = NULL;
    Py_XDECREF(self->callable);

    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
PyPropertyTree_Iter__tp_iter(PyPropertyTree_Iter *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
PyPropertyTree_Iter__tp_iternext(PyPropertyTree_Iter *self)
{
    while (1)
    {
        boost::property_tree::ptree::iterator iter = *self->iterator;

        if (iter == self->container->obj->end())
        {
            PyErr_SetNone(PyExc_StopIteration);
            return NULL;
        }

        ++(*self->iterator);

        std::string key = iter->first;

        PyPropertyTree *py_ptree = PyPropertyTree_New(&iter->second, PTREE_FLAG_OBJECT_NOT_OWNED);

        if (self->callable)
        {
            PyObject *retval = PyObject_CallFunction(self->callable, (char *)"s#O", key.c_str(), key.size(), py_ptree);

            if (!retval)
            {
                Py_DECREF(py_ptree);
                /* keep whatever exception PyObject_CallFunction threw */
                return NULL;
            }
            else if (PyObject_IsTrue(retval))
            {
                Py_DECREF(retval);
            }
            else
            {
                Py_DECREF(retval);
                Py_DECREF(py_ptree);
                continue;
            }
        }

        return Py_BuildValue((char *)"s#N", key.c_str(), key.size(), py_ptree);
    }
}

PyTypeObject PyPropertyTree_IterType = {
    PyVarObject_HEAD_INIT(NULL, 0)(char *) "proptree.TreeIter", /* tp_name */
    sizeof(PyPropertyTree_Iter),                                /* tp_basicsize */
    0,                                                          /* tp_itemsize */
    (destructor)PyPropertyTree_Iter__tp_dealloc,                /* tp_dealloc */
    (printfunc)0,                                               /* tp_print */
    (getattrfunc)NULL,                                          /* tp_getattr */
    (setattrfunc)NULL,                                          /* tp_setattr */
    (PyAsyncMethods *)NULL,                                     /* tp_compare */
    (reprfunc)NULL,                                             /* tp_repr */
    (PyNumberMethods *)NULL,                                    /* tp_as_number */
    (PySequenceMethods *)NULL,                                  /* tp_as_sequence */
    (PyMappingMethods *)NULL,                                   /* tp_as_mapping */
    (hashfunc)NULL,                                             /* tp_hash */
    (ternaryfunc)NULL,                                          /* tp_call */
    (reprfunc)NULL,                                             /* tp_str */
    (getattrofunc)NULL,                                         /* tp_getattro */
    (setattrofunc)NULL,                                         /* tp_setattro */
    (PyBufferProcs *)NULL,                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,                    /* tp_flags */
    NULL,                                                       /* Documentation string */
    (traverseproc)PyPropertyTree_Iter__tp_traverse,             /* tp_traverse */
    (inquiry)PyPropertyTree_Iter__tp_clear,                     /* tp_clear */
    (richcmpfunc)NULL,                                          /* tp_richcompare */
    0,                                                          /* tp_weaklistoffset */
    (getiterfunc)PyPropertyTree_Iter__tp_iter,                  /* tp_iter */
    (iternextfunc)PyPropertyTree_Iter__tp_iternext,             /* tp_iternext */
    (struct PyMethodDef *)NULL,                                 /* tp_methods */
    (struct PyMemberDef *)0,                                    /* tp_members */
    NULL,                                                       /* tp_getset */
    NULL,                                                       /* tp_base */
    NULL,                                                       /* tp_dict */
    (descrgetfunc)NULL,                                         /* tp_descr_get */
    (descrsetfunc)NULL,                                         /* tp_descr_set */
    0,                                                          /* tp_dictoffset */
    (initproc)NULL,                                             /* tp_init */
    (allocfunc)PyType_GenericAlloc,                             /* tp_alloc */
    (newfunc)PyType_GenericNew,                                 /* tp_new */
    (freefunc)0,                                                /* tp_free */
    (inquiry)NULL,                                              /* tp_is_gc */
    NULL,                                                       /* tp_bases */
    NULL,                                                       /* tp_mro */
    NULL,                                                       /* tp_cache */
    NULL,                                                       /* tp_subclasses */
    NULL,                                                       /* tp_weaklist */
    (destructor)NULL                                            /* tp_del */
};

static void
PyPropertyTree_AssocIter__tp_clear(PyPropertyTree_AssocIter *self)
{
    Py_CLEAR(self->container);
}

static int
PyPropertyTree_AssocIter__tp_traverse(PyPropertyTree_AssocIter *self, visitproc visit, void *arg)
{
    Py_VISIT((PyObject *)self->container);
    return 0;
}

static void
PyPropertyTree_AssocIter__tp_dealloc(PyPropertyTree_AssocIter *self)
{
    Py_CLEAR(self->container);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
PyPropertyTree_AssocIter__tp_iter(PyPropertyTree_AssocIter *self)
{
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *
PyPropertyTree_AssocIter__tp_iternext(PyPropertyTree_AssocIter *self)
{
    boost::property_tree::ptree::assoc_iterator iter = self->iterator.first;

    if (iter == self->iterator.second)
    {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    self->iterator.first++;

    const std::string &key = iter->first;

    PyPropertyTree *py_ptree = PyPropertyTree_New(&iter->second, PTREE_FLAG_OBJECT_NOT_OWNED);

    return Py_BuildValue((char *)"s#N", key.c_str(), key.size(), py_ptree);
}

PyTypeObject PyPropertyTree_AssocIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)(char *) "proptree.AssocTreeIter", /* tp_name */
    sizeof(PyPropertyTree_AssocIter),                                /* tp_basicsize */
    0,                                                               /* tp_itemsize */
    (destructor)PyPropertyTree_AssocIter__tp_dealloc,                /* tp_dealloc */
    (printfunc)0,                                                    /* tp_print */
    (getattrfunc)NULL,                                               /* tp_getattr */
    (setattrfunc)NULL,                                               /* tp_setattr */
    (PyAsyncMethods *)NULL,                                          /* tp_compare */
    (reprfunc)NULL,                                                  /* tp_repr */
    (PyNumberMethods *)NULL,                                         /* tp_as_number */
    (PySequenceMethods *)NULL,                                       /* tp_as_sequence */
    (PyMappingMethods *)NULL,                                        /* tp_as_mapping */
    (hashfunc)NULL,                                                  /* tp_hash */
    (ternaryfunc)NULL,                                               /* tp_call */
    (reprfunc)NULL,                                                  /* tp_str */
    (getattrofunc)NULL,                                              /* tp_getattro */
    (setattrofunc)NULL,                                              /* tp_setattro */
    (PyBufferProcs *)NULL,                                           /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,                         /* tp_flags */
    NULL,                                                            /* Documentation string */
    (traverseproc)PyPropertyTree_AssocIter__tp_traverse,             /* tp_traverse */
    (inquiry)PyPropertyTree_AssocIter__tp_clear,                     /* tp_clear */
    (richcmpfunc)NULL,                                               /* tp_richcompare */
    0,                                                               /* tp_weaklistoffset */
    (getiterfunc)PyPropertyTree_AssocIter__tp_iter,                  /* tp_iter */
    (iternextfunc)PyPropertyTree_AssocIter__tp_iternext,             /* tp_iternext */
    (struct PyMethodDef *)NULL,                                      /* tp_methods */
    (struct PyMemberDef *)0,                                         /* tp_members */
    NULL,                                                            /* tp_getset */
    NULL,                                                            /* tp_base */
    NULL,                                                            /* tp_dict */
    (descrgetfunc)NULL,                                              /* tp_descr_get */
    (descrsetfunc)NULL,                                              /* tp_descr_set */
    0,                                                               /* tp_dictoffset */
    (initproc)NULL,                                                  /* tp_init */
    (allocfunc)PyType_GenericAlloc,                                  /* tp_alloc */
    (newfunc)PyType_GenericNew,                                      /* tp_new */
    (freefunc)0,                                                     /* tp_free */
    (inquiry)NULL,                                                   /* tp_is_gc */
    NULL,                                                            /* tp_bases */
    NULL,                                                            /* tp_mro */
    NULL,                                                            /* tp_cache */
    NULL,                                                            /* tp_subclasses */
    NULL,                                                            /* tp_weaklist */
    (destructor)NULL                                                 /* tp_del */
};

/* --- property_tree.json module --- */

PyDoc_STRVAR(property_tree_read_json__doc__,
             "loads(str) -> Tree\n\n"
             "    Read JSON from a the given string and translate it to a property tree.\n"
             "    * Items of JSON arrays are translated into ptree keys with empty\n"
             "      names. Members of objects are translated into named keys.\n"
             "    * JSON data can be a string, a numeric value, or one of literals\n"
             "      \"null\", \"true\" and \"false\". During parse, any of the above is\n"
             "      copied verbatim into ptree data string.\n");

static PyObject *
property_tree_read_json(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::istringstream stream;
    const char *string_char;
    Py_ssize_t string_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"str", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#:loads", (char **)keywords,
                                     &string_char, &string_len))
    {
        return NULL;
    }

    stream = std::istringstream(std::string(string_char, string_len));
    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_json(stream, *tree->obj);
    }
    catch (boost::property_tree::json_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeJSONParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

PyDoc_STRVAR(property_tree_read_json_file__doc__,
             "load(filename) -> Tree\n\n"
             "    Read JSON from a the given file and translate it to a property tree.\n"
             "    * Items of JSON arrays are translated into ptree keys with empty\n"
             "      names. Members of objects are translated into named keys.\n"
             "    * JSON data can be a string, a numeric value, or one of literals\n"
             "      \"null\", \"true\" and \"false\". During parse, any of the above is\n"
             "      copied verbatim into ptree data string.\n");

static PyObject *
property_tree_read_json_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"filename", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#:load", (char **)keywords,
                                     &filename, &filename_len))
    {
        return NULL;
    }

    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_json(std::string(filename, filename_len), *tree->obj);
    }
    catch (boost::property_tree::json_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeJSONParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

PyDoc_STRVAR(property_tree_write_json__doc__,
             "dumps(tree, pretty_print=True) -> str\n\n"
             "    Translates the property tree to JSON.\n"
             "    * Any property tree key containing only unnamed subkeys will be\n"
             "      rendered as JSON arrays.\n"
             "    * Tree cannot contain keys that have both subkeys and non-empty data.\n"
             "    @param tree         - The property tree to tranlsate to JSON and output.\n"
             "    @param pretty_print - Whether to pretty-print.\n");

static PyObject *
property_tree_write_json(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::ostringstream stream;
    std::string stream_std;
    PyPropertyTree *tree;
    int pretty_print = 1;
    const char *keywords[] = {"tree", "pretty_print", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"O!|p:dumps", (char **)keywords,
                                     &PyPropertyTree_Type, &tree, &pretty_print))
    {
        return NULL;
    }

    try
    {
        boost::property_tree::write_json(stream, *tree->obj, pretty_print);
    }
    catch (boost::property_tree::json_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeJSONParserError_Type, exc.what());
        return NULL;
    }

    stream_std = stream.str();

    return PyUnicode_DecodeUTF8(stream_std.c_str(), stream_std.size(), NULL);
}

PyDoc_STRVAR(property_tree_write_json_file__doc__,
             "dump(filename, tree, pretty_print=True)\n\n"
             "    Translates the property tree to JSON and writes it the given file.\n"
             "    * Any property tree key containing only unnamed subkeys will be\n"
             "      rendered as JSON arrays.\n"
             "    * Tree cannot contain keys that have both subkeys and non-empty data.\n"
             "    @param filename     - The name of the file to which to write the JSON\n"
             "                          representation of the property tree.\n"
             "    @param tree         - The property tree to tranlsate to JSON and output.\n"
             "    @param pretty_print - Whether to pretty-print.\n");

static PyObject *
property_tree_write_json_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    int pretty_print = 1;
    const char *keywords[] = {"filename", "tree", "pretty_print", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O!|p:dump", (char **)keywords,
                                     &filename, &filename_len, &PyPropertyTree_Type, &tree, &pretty_print))
    {
        return NULL;
    }

    try
    {
        boost::property_tree::write_json(std::string(filename, filename_len), *tree->obj, std::locale(), pretty_print);
    }
    catch (boost::property_tree::json_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeJSONParserError_Type, exc.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef property_tree_json_functions[] = {
    {(char *)"loads",
     (PyCFunction)property_tree_read_json,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_read_json__doc__},
    {(char *)"load",
     (PyCFunction)property_tree_read_json_file,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_read_json_file__doc__},
    {(char *)"dumps",
     (PyCFunction)property_tree_write_json,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_write_json__doc__},
    {(char *)"dump",
     (PyCFunction)property_tree_write_json_file,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_write_json_file__doc__},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef property_tree_json_moduledef = {
    PyModuleDef_HEAD_INIT,
    "proptree.json",
    NULL,
    -1,
    property_tree_json_functions,
};

/* --- property_tree.xml module --- */

PyDoc_STRVAR(property_tree_read_xml__doc__,
             "loads(str, flags=0) -> Tree\n\n"
             "    Reads XML from a string and translates it to property tree.\n"
             "     * XML attributes are placed under keys named <xmlattr>.\n"
             "     @param str   - String from which to read in the property tree.\n"
             "     @param flags - Flags controlling the behaviour of the parser.\n"
             "            XML_NO_CONCAT_TEXT  -- Prevents concatenation of text nodes into\n"
             "                                   datastring of property tree. Puts them in\n"
             "                                   separate <xmltext> strings instead.\n"
             "            XML_NO_COMMENTS     -- Skip XML comments.\n"
             "            XML_TRIM_WHITESPACE -- Trim leading and trailing whitespace from\n"
             "                                   text and collapse sequences of whitespace.\n");

static PyObject *
property_tree_read_xml(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::istringstream stream;
    const char *stream_char;
    Py_ssize_t stream_len;
    PyPropertyTree *tree;
    int flags = 0;
    const char *keywords[] = {"str", "flags", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#|i:loads", (char **)keywords,
                                     &stream_char, &stream_len, &flags))
    {
        return NULL;
    }
    stream = std::istringstream(std::string(stream_char, stream_len));
    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_xml(stream, *tree->obj, flags);
    }
    catch (boost::property_tree::xml_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeXMLParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

PyDoc_STRVAR(property_tree_read_xml_file__doc__,
             "load(filename, flags=0) -> Tree\n\n"
             "    Reads XML from a file and translates it to property tree.\n"
             "     * XML attributes are placed under keys named <xmlattr>.\n"
             "     @param filename   - File to read from.\n"
             "     @param flags      - Flags controlling the behaviour of the parser.\n"
             "            XML_NO_CONCAT_TEXT  -- Prevents concatenation of text nodes into\n"
             "                                   datastring of property tree. Puts them in\n"
             "                                   separate <xmltext> strings instead.\n"
             "            XML_NO_COMMENTS     -- Skip XML comments.\n"
             "            XML_TRIM_WHITESPACE -- Trim leading and trailing whitespace from\n"
             "                                   text and collapse sequences of whitespace.\n");

static PyObject *
property_tree_read_xml_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    int flags = 0;
    const char *keywords[] = {"filename", "flags", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#|i:load", (char **)keywords,
                                     &filename, &filename_len, &flags))
    {
        return NULL;
    }

    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_xml(std::string(filename, filename_len), *tree->obj, flags);
    }
    catch (boost::property_tree::xml_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeXMLParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

PyDoc_STRVAR(property_tree_write_xml__doc__,
             "dumps(tree) -> str\n\n"
             "    Translates the property tree to XML.\n");

static PyObject *
property_tree_write_xml(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::ostringstream stream;
    std::string stream_std;
    PyPropertyTree *tree;
    const char *keywords[] = {"tree", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"O!:dumps", (char **)keywords,
                                     &PyPropertyTree_Type, &tree))
    {
        return NULL;
    }
    try
    {
        boost::property_tree::write_xml(stream, *((PyPropertyTree *)tree)->obj);
    }
    catch (boost::property_tree::xml_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeXMLParserError_Type, exc.what());
        return NULL;
    }
    stream_std = stream.str();

    return PyUnicode_DecodeUTF8(stream_std.c_str(), stream_std.size(), NULL);
}

PyDoc_STRVAR(property_tree_write_xml_file__doc__,
             "dump(filename, tree)\n\n"
             "    Translates the property tree to XML and writes it the given file.\n");

static PyObject *
property_tree_write_xml_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"filename", "tree", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O!:dump", (char **)keywords,
                                     &filename, &filename_len, &PyPropertyTree_Type, &tree))
    {
        return NULL;
    }

    try
    {
        boost::property_tree::write_xml(std::string(filename, filename_len), *tree->obj);
    }
    catch (boost::property_tree::xml_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeXMLParserError_Type, exc.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef property_tree_xml_functions[] = {
    {(char *)"loads",
     (PyCFunction)property_tree_read_xml,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_read_xml__doc__},
    {(char *)"load",
     (PyCFunction)property_tree_read_xml_file,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_read_xml_file__doc__},
    {(char *)"dumps",
     (PyCFunction)property_tree_write_xml,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_write_xml__doc__},
    {(char *)"dump",
     (PyCFunction)property_tree_write_xml_file,
     METH_KEYWORDS | METH_VARARGS,
     property_tree_write_xml_file__doc__},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef property_tree_xml_moduledef = {
    PyModuleDef_HEAD_INIT,
    "proptree.xml",
    NULL,
    -1,
    property_tree_xml_functions,
};

/* --- property_tree.ini module --- */

static PyObject *
property_tree_read_ini(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::istringstream stream;
    const char *stream_char;
    Py_ssize_t stream_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"str", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#", (char **)keywords, &stream_char, &stream_len))
    {
        return NULL;
    }

    stream = std::istringstream(std::string(stream_char, stream_len));
    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_ini(stream, *tree->obj);
    }
    catch (boost::property_tree::ini_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINIParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

static PyObject *
property_tree_read_ini_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"filename", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#", (char **)keywords, &filename, &filename_len))
    {
        return NULL;
    }

    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_ini(std::string(filename, filename_len), *tree->obj);
    }
    catch (boost::property_tree::ini_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINIParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

static PyObject *
property_tree_write_ini(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::ostringstream stream;
    std::string stream_std;
    PyPropertyTree *tree;
    const char *keywords[] = {"tree", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"O!", (char **)keywords, &PyPropertyTree_Type, &tree))
    {
        return NULL;
    }
    try
    {
        boost::property_tree::write_ini(stream, *tree->obj);
    }
    catch (boost::property_tree::ini_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINIParserError_Type, exc.what());
        return NULL;
    }
    stream_std = stream.str();

    return PyUnicode_DecodeUTF8(stream_std.c_str(), stream_std.size(), NULL);
}

static PyObject *
property_tree_write_ini_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"filename", "tree", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O!", (char **)keywords,
                                     &filename, &filename_len, &PyPropertyTree_Type, &tree))
    {
        return NULL;
    }

    try
    {
        boost::property_tree::write_ini(std::string(filename, filename_len), *tree->obj);
    }
    catch (boost::property_tree::ini_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINIParserError_Type, exc.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef property_tree_ini_functions[] = {
    {(char *)"loads",
     (PyCFunction)property_tree_read_ini,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {(char *)"load",
     (PyCFunction)property_tree_read_ini_file,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {(char *)"dumps",
     (PyCFunction)property_tree_write_ini,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {(char *)"dump",
     (PyCFunction)property_tree_write_ini_file,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef property_tree_ini_moduledef = {
    PyModuleDef_HEAD_INIT,
    "proptree.ini",
    NULL,
    -1,
    property_tree_ini_functions,
};

/* --- property_tree.info module --- */

static PyObject *
property_tree_read_info(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::istringstream stream;
    const char *stream_char;
    Py_ssize_t stream_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"stream", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#", (char **)keywords, &stream_char, &stream_len))
    {
        return NULL;
    }

    stream = std::istringstream(std::string(stream_char, stream_len));
    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_info(stream, *tree->obj);
    }
    catch (boost::property_tree::info_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINFOParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

static PyObject *
property_tree_read_info_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"filename", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#", (char **)keywords, &filename, &filename_len))
    {
        return NULL;
    }

    tree = PyPropertyTree_New(new boost::property_tree::ptree(), PTREE_FLAG_NONE);

    try
    {
        boost::property_tree::read_info(std::string(filename, filename_len), *tree->obj);
    }
    catch (boost::property_tree::info_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINFOParserError_Type, exc.what());
        Py_DECREF(tree);
        return NULL;
    }

    return (PyObject *)tree;
}

static PyObject *
property_tree_write_info(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    std::ostringstream stream;
    std::string stream_std;
    PyPropertyTree *tree;
    const char *keywords[] = {"tree", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"O!", (char **)keywords, &PyPropertyTree_Type, &tree))
    {
        return NULL;
    }
    try
    {
        boost::property_tree::write_info(stream, *tree->obj);
    }
    catch (boost::property_tree::info_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINFOParserError_Type, exc.what());
        return NULL;
    }
    stream_std = stream.str();

    return PyUnicode_DecodeUTF8(stream_std.c_str(), stream_std.size(), NULL);
}

static PyObject *
property_tree_write_info_file(PyObject *Py_UNUSED(dummy), PyObject *args, PyObject *kwargs)
{
    const char *filename = NULL;
    Py_ssize_t filename_len;
    PyPropertyTree *tree;
    const char *keywords[] = {"filename", "tree", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, (char *)"s#O!", (char **)keywords,
                                     &filename, &filename_len, &PyPropertyTree_Type, &tree))
    {
        return NULL;
    }

    try
    {
        boost::property_tree::write_info(std::string(filename, filename_len), *tree->obj);
    }
    catch (boost::property_tree::info_parser_error const &exc)
    {
        PyErr_SetString((PyObject *)PyPropertyTreeINFOParserError_Type, exc.what());
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef property_tree_info_functions[] = {
    {(char *)"loads",
     (PyCFunction)property_tree_read_info,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {(char *)"load",
     (PyCFunction)property_tree_read_info_file,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {(char *)"dumps",
     (PyCFunction)property_tree_write_info,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {(char *)"dump",
     (PyCFunction)property_tree_write_info_file,
     METH_KEYWORDS | METH_VARARGS,
     NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef property_tree_info_moduledef = {
    PyModuleDef_HEAD_INIT,
    "proptree.info",
    NULL,
    -1,
    property_tree_info_functions,
};

/* --- property_tree module --- */

static struct PyModuleDef property_tree_moduledef = {
    PyModuleDef_HEAD_INIT,
    "proptree",
    NULL,
    -1,
    NULL,
};

#if defined(__cplusplus)
extern "C"
#endif
#if defined(__GNUC__) && __GNUC__ >= 4
    __attribute__((visibility("default")))
#endif

    PyObject *
    PyInit_proptree(void)
{
    PyObject *m, *submodule;

    m = PyModule_Create(&property_tree_moduledef);

    if (m == NULL)
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::ptree' class */

    if (PyType_Ready(&PyPropertyTree_Type))
    {
        return NULL;
    }

    PyModule_AddObject(m, (char *)"Tree", (PyObject *)&PyPropertyTree_Type);

    /* Register the 'boost::property_tree::ptree::iterator' class */

    if (PyType_Ready(&PyPropertyTree_IterType))
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::ptree::assoc_iterator' class */

    if (PyType_Ready(&PyPropertyTree_AssocIterType))
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::ptree_bad_data' exception */

    if ((PyPropertyTreeBadDataError_Type = (PyTypeObject *)PyErr_NewException((char *)"proptree.BadDataError", NULL, NULL)) == NULL)
    {
        return NULL;
    }

    Py_INCREF((PyObject *)PyPropertyTreeBadDataError_Type);
    PyModule_AddObject(m, (char *)"BadDataError", (PyObject *)PyPropertyTreeBadDataError_Type);

    /* Register the 'boost::property_tree::ptree_bad_path' exception */

    if ((PyPropertyTreeBadPathError_Type = (PyTypeObject *)PyErr_NewException((char *)"proptree.BadPathError", NULL, NULL)) == NULL)
    {
        return NULL;
    }

    Py_INCREF((PyObject *)PyPropertyTreeBadPathError_Type);
    PyModule_AddObject(m, (char *)"BadPathError", (PyObject *)PyPropertyTreeBadPathError_Type);

    /* Register the property_tree.json submodule */

    submodule = PyModule_Create(&property_tree_json_moduledef);

    if (submodule == NULL)
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::json_parser_error' exception */

    if (!(PyPropertyTreeJSONParserError_Type = (PyTypeObject *)PyErr_NewException((char *)"proptree.json.JSONParserError", NULL, NULL)))
    {
        return NULL;
    }

    Py_INCREF((PyObject *)PyPropertyTreeJSONParserError_Type);
    PyModule_AddObject(submodule, (char *)"JSONParserError", (PyObject *)PyPropertyTreeJSONParserError_Type);

    Py_INCREF(submodule);
    PyModule_AddObject(m, (char *)"json", submodule);

    /* Register the property_tree.xml submodule */

    submodule = PyModule_Create(&property_tree_xml_moduledef);

    if (submodule == NULL)
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::xml_parser_error' exception */

    if (!(PyPropertyTreeXMLParserError_Type = (PyTypeObject *)PyErr_NewException((char *)"proptree.xml.XMLParserError", NULL, NULL)))
    {
        return NULL;
    }

    Py_INCREF((PyObject *)PyPropertyTreeXMLParserError_Type);
    PyModule_AddObject(submodule, (char *)"XMLParserError", (PyObject *)PyPropertyTreeXMLParserError_Type);

    PyModule_AddIntConstant(submodule, (char *)"XML_NO_CONCAT_TEXT", 0x1);
    PyModule_AddIntConstant(submodule, (char *)"XML_NO_COMMENTS", 0x2);
    PyModule_AddIntConstant(submodule, (char *)"XML_TRIM_WHITESPACE", 0x4);

    Py_INCREF(submodule);
    PyModule_AddObject(m, (char *)"xml", submodule);

    /* Register the property_tree.ini submodule */

    submodule = PyModule_Create(&property_tree_ini_moduledef);

    if (submodule == NULL)
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::ini_parser_error' exception */

    if (!(PyPropertyTreeINIParserError_Type = (PyTypeObject *)PyErr_NewException((char *)"proptree.ini.INIParserError", NULL, NULL)))
    {
        return NULL;
    }

    Py_INCREF((PyObject *)PyPropertyTreeINIParserError_Type);
    PyModule_AddObject(submodule, (char *)"INIParserError", (PyObject *)PyPropertyTreeINIParserError_Type);

    Py_INCREF(submodule);
    PyModule_AddObject(m, (char *)"ini", submodule);

    /* Register the property_tree.info submodule */

    submodule = PyModule_Create(&property_tree_info_moduledef);

    if (submodule == NULL)
    {
        return NULL;
    }

    /* Register the 'boost::property_tree::info_parser_error' exception */

    if (!(PyPropertyTreeINFOParserError_Type = (PyTypeObject *)PyErr_NewException((char *)"proptree.INFOParserError", NULL, NULL)))
    {
        return NULL;
    }

    Py_INCREF((PyObject *)PyPropertyTreeINFOParserError_Type);
    PyModule_AddObject(submodule, (char *)"INFOParserError", (PyObject *)PyPropertyTreeINFOParserError_Type);

    Py_INCREF(submodule);
    PyModule_AddObject(m, (char *)"info", submodule);

    return m;
}

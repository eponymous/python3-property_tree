## Python bindings for boost::property_tree

https://github.com/boostorg/property_tree

Tried to keep things pythonic so this isn't a 1:1 mapping to the C++ API

The property_tree.Tree class is a mix between a list and mapping structure so
the python functions (mostly) match either 'list' or 'dict' where appropriate.

### Building

boost::property_tree is a header-only library so no runtime library dependencies

    python3 setup.py build

### API Docs

#### class Tree()
    A property tree is a hierarchical data structure which has one data element in each node
    as well as an ordered sequence of sub-nodes which are additionally identified by a non-unique key.
    
    __init__(self, value=None) -> Tree
        Creates a node with no children and a copy of the given data if provided.
    
    add(self, path, value) -> Tree
        Add a node at the given path with the given value.
          If the node identified by the path does not exist, create it and all its missing parents.
          If the node already exists, add a sibling with the same key.
    
    append(self, key, value) -> Tree
        Add the value to the end of the child list with the given key.
    
    clear(self)
        Clear this Tree completely of both children and data.
    
    count(self, key) -> int
        Count the number of direct children with the given key.
    
    empty(self) -> bool
        Check whether this tree contains any children.
    
    erase(self, key) -> int
        Erase all the children with the given key and return the count.
    
    extend(self, iterator)
        Extend the tree by appending all the items from iterator.
    
    find(self, key) -> Tree
        Find a child with the given key or None.
          There is no guarantee about which child is returned if multiple have the same key.
    
    get(self, path, default=None) -> Tree
        Get the child node at the given path, else return default value.
        If default is not provided a BadPathError is raised.
    
    index(self, key, start=0, end=-1)
        Return zero-based index in the list of the first item whose value is equal to key.
    
    insert(self, index, key, value)
        Insert a copy of the given tree with its key just before the given index in this node.
    
    items(self) -> iterator
        Return an iterator to the ((key, value) pairs) of children.
    
    keys(self) -> list
        Get a list of all the child keys.
    
    pop(self, key, default=None) -> Tree
        Remove the child with the given key and return its value, else default.
          If default is not given and key is not in the tree, a KeyError is raised.
          There is no guarantee about which child is returned if multiple have the same key.
    
    popitem(self, index=-1) -> (key, value)
        Remove and return the child at the given index.
        If no index is specified remove and return the last child.
    
    put(self, path, value) -> Tree
        Set the node at the given path to the given value.
        If the node identified by the path does not exist, create it and all its missing parents.
        If the node at the path already exists, replace its value.
    
    remove(self, key)
        Remove the first child whose value is equal to key.
    
    reverse(self)
        Reverse the children in place.
    
    search(self, arg) -> iterator
        Return an iterator to the children that match this key.
        Argument can be a string or function of the type: func(key, value) -> Bool
    
    setdefault(self, path, default=None) -> Tree
        If path is in the tree, return its value.
        If the node identified by the path does not exist, create it and all its missing parents.
    
    sort(self, key=None)
        Sort the children according to key order or a callable object.
    
    sorted(self) -> iterator
        Get an iterator to the sorted children of this node, in key order.
    
    values(self) -> list
        Get a list of the children values.
    
    value
        The string value of this node

#### property_tree.json

    dump(filename, tree, pretty_print=True)
        Translates the property tree to JSON and writes it the given file.
    
          Any property tree key containing only unnamed subkeys will be
          rendered as JSON arrays.
    
          Tree cannot contain keys that have both subkeys and non-empty data.
    
          @param filename     - The name of the file to which to write the JSON
                                representation of the property tree.
    
          @param tree         - The property tree to tranlsate to JSON and output.
    
          @param pretty_print - Whether to pretty-print.
    
    dumps(tree, pretty_print=True) -> str
        Translates the property tree to JSON.
          Any property tree key containing only unnamed subkeys will be rendered as JSON arrays.
          Tree cannot contain keys that have both subkeys and non-empty data.
    
          @param tree         - The property tree to tranlsate to JSON and output.
    
          @param pretty_print - Whether to pretty-print.
    
    load(filename) -> Tree
        Read JSON from a the given file and translate it to a property tree.
          Items of JSON arrays are translated into ptree keys with empty names.
          Members of objects are translated into named keys.

          JSON data can be a string, a numeric value, or one of literals
          "null", "true" and "false". During parse, any of the above is
          copied verbatim into ptree data string.
    
    loads(str) -> Tree
        Read JSON from a the given string and translate it to a property tree.
          Items of JSON arrays are translated into ptree keys with empty names.
          Members of objects are translated into named keys.
    
          JSON data can be a string, a numeric value, or one of literals
          "null", "true" and "false". During parse, any of the above is
          copied verbatim into ptree data string.

#### property_tree.xml

    dump(filename, tree)
        Translates the property tree to XML and writes it the given file.
    
    dumps(tree) -> str
        Translates the property tree to XML.
    
    load(filename, flags=0) -> Tree
        Reads XML from a file and translates it to property tree.
          XML attributes are placed under keys named <xmlattr>.
    
          @param filename   - File to read from.
    
          @param flags      - Flags controlling the behaviour of the parser.
              XML_NO_CONCAT_TEXT  -- Prevents concatenation of text nodes into
                                     datastring of property tree. Puts them in
                                     separate <xmltext> strings instead.
    
              XML_NO_COMMENTS     -- Skip XML comments.
    
              XML_TRIM_WHITESPACE -- Trim leading and trailing whitespace from
                                     text and collapse sequences of whitespace.
    
    loads(str, flags=0) -> Tree
        Reads XML from a string and translates it to property tree.
          XML attributes are placed under keys named <xmlattr>.
    
          @param str   - String from which to read in the property tree.
    
          @param flags - Flags controlling the behaviour of the parser.
                XML_NO_CONCAT_TEXT  -- Prevents concatenation of text nodes into
                                       datastring of property tree. Puts them in
                                       separate <xmltext> strings instead.
    
                XML_NO_COMMENTS     -- Skip XML comments.
    
                XML_TRIM_WHITESPACE -- Trim leading and trailing whitespace from
                                       text and collapse sequences of whitespace.


### TODO

- [ ] docs for property_tree.ini submodule
- [ ] docs for property_tree.info submodule
- [ ] more tests

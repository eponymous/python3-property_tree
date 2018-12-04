import unittest
import copy
import property_tree as ptree

class TestTree(unittest.TestCase):
    def test_insertions(self):
        pt = ptree.Tree()

        tmp1 = ptree.Tree("data1")
        tmp2 = ptree.Tree("data2")
        tmp3 = ptree.Tree("data3")
        tmp4 = ptree.Tree("data4")

        it1 = pt.add("key1", tmp1)
        it2 = pt.insert(pt.index("key1"), "key2", tmp2)
        it3 = it1.put("key3", tmp3)
        it4 = it1.append("key4", tmp4)
        it2 += it1

        self.assertEqual(pt.get("key1", ""),      "data1")
        self.assertEqual(pt.get("key2", ""),      "data2")
        self.assertEqual(pt.get("key1.key3", ""), "data3")
        self.assertEqual(pt.get("key1.key4", ""), "data4")
        self.assertEqual(pt.get("key2.key3", ""), "data3")
        self.assertEqual(pt.get("key2.key4", ""), "data4")

        ## __getattribute__
        self.assertEqual(pt.key1,      "data1")
        self.assertEqual(pt.key2,      "data2")
        self.assertEqual(pt.key1.key3, "data3")
        self.assertEqual(pt.key1.key4, "data4")
        self.assertEqual(pt.key2.key3, "data3")
        self.assertEqual(pt.key2.key4, "data4")

    def test_erasing(self):
        pt = ptree.Tree()

        tmp1 = ptree.Tree("data1")
        tmp2 = ptree.Tree("data2")
        tmp3 = ptree.Tree("data3")
        tmp4 = ptree.Tree("data4")

        it1 = pt.add("key1", tmp1)
        it2 = pt.insert(pt.index("key1"), "key2", tmp2)

        it1.append("key", tmp3)
        it1.insert(0, "key", tmp4)
        it2.extend(it1.items())

        # Test single erase
        n = pt.erase("key1")
        self.assertEqual(n, 1)

        # Test multiple erase
        n = it2.erase("key")
        self.assertEqual(n, 2)

        # Test one more erase
        n = pt.erase("key2")
        self.assertEqual(n, 1)

    def test_clear(self):
        # Do insertions
        pt = ptree.Tree("data")
        pt.append("key", ptree.Tree("data"))

        # Test clear
        pt.clear()
        self.assertTrue(pt.empty())
        self.assertEqual(pt.value, '')

    def test_insert(self):
        pt = ptree.Tree()

        tmp1 = ptree.Tree("data1")
        tmp2 = ptree.Tree("data2")
        tmp3 = ptree.Tree("data3")
        tmp4 = ptree.Tree("data4")

        pt.insert(-1, "key3", tmp3)
        pt.insert(0,  "key2", tmp2)
        pt.insert(-1, "key4", tmp4)
        pt.insert(0,  "key1", tmp1)

        # Check sequence
        keys = pt.keys()
        self.assertEqual(keys[0], "key1")
        self.assertEqual(keys[1], "key2")
        self.assertEqual(keys[2], "key3")
        self.assertEqual(keys[3], "key4")

        # Test pops
        pt.popitem()
        self.assertEqual(pt[0],  "data1")
        self.assertEqual(pt[-1], "data3")

        pt.popitem(0)
        self.assertEqual(pt[0],  "data2")
        self.assertEqual(pt[-1], "data3")

        pt.popitem(-1)
        self.assertEqual(pt[0],  "data2")
        self.assertEqual(pt[-1], "data2")

        pt.popitem(0)
        self.assertTrue(pt.empty())

    def test_comparison(self):
        # Prepare original
        pt_orig = ptree.Tree("data")
        pt_orig.put("key1",      "data1")
        pt_orig.put("key1.key3", "data2")
        pt_orig.put("key1.key4", "data3")
        pt_orig.put("key2",      "data4")

        pt1 = ptree.Tree(pt_orig)
        pt2 = ptree.Tree(pt_orig)

        self.assertEqual(pt1, pt2)
        self.assertEqual(pt2, pt1)

        # Test originals with modified case
        pt1 = ptree.Tree(pt_orig)
        pt2 = ptree.Tree(pt_orig)

        pt1.popitem()
        pt1.put("KEY2", ptree.Tree("data4"))

        self.assertNotEqual(pt1, pt2)
        self.assertNotEqual(pt2, pt1)

        # Test modified copies (both modified the same way)
        pt1 = copy.copy(pt_orig)
        pt2 = copy.copy(pt_orig)

        pt1.put("key1.key5", ptree.Tree("."))
        pt2.put("key1.key5", ptree.Tree("."))

        self.assertEqual(pt1, pt2)
        self.assertEqual(pt2, pt1)

        # Test modified copies (modified root data)
        pt1 = copy.copy(pt_orig)
        pt2 = copy.copy(pt_orig)

        pt1.value = "a"
        pt2.value = "b"

        self.assertNotEqual(pt1, pt2)
        self.assertNotEqual(pt2, pt1)

        # Test modified copies (added subkeys with different data)
        pt1 = copy.copy(pt_orig)
        pt2 = copy.copy(pt_orig)

        pt1.put("key1.key5", ptree.Tree("a"))
        pt2.put("key1.key5", ptree.Tree("b"))

        self.assertNotEqual(pt1, pt2)
        self.assertNotEqual(pt2, pt1)

        # Test modified copies (added subkeys with different keys)
        pt1 = copy.copy(pt_orig)
        pt2 = copy.copy(pt_orig)
        pt1.put("key1.key5", ptree.Tree(""))
        pt2.put("key1.key6", ptree.Tree(""))

        self.assertNotEqual(pt1, pt2)
        self.assertNotEqual(pt2, pt1)

        # Test modified copies (added subkey to only one copy)
        pt1 = copy.copy(pt_orig)
        pt2 = copy.copy(pt_orig)
        pt1.put("key1.key5", ptree.Tree(""))

        self.assertNotEqual(pt1, pt2)
        self.assertNotEqual(pt2, pt1)

    def test_get_set(self):
        # Do insertions
        pt = ptree.Tree()
        pt.put("k1",       1)
        pt.put("k2.k",     2.5)
        pt.put("k3.k.k",   "ala ma kota")
        pt.put("k5.k.k.f", False)
        pt.put("k5.k.k.t", True)
        pt.setdefault("k5.k.k.n")

        # Do extractions via get
        self.assertEqual(pt.get("k1"),         1)
        self.assertEqual(pt.get("k2.k"),     2.5)
        self.assertEqual(pt.get("k3.k.k"),   "ala ma kota")
        self.assertEqual(pt.get("k5.k.k.f"), False)
        self.assertEqual(pt.get("k5.k.k.t"), True)
        self.assertEqual(pt.get("k5.k.k.n"), None)
        self.assertEqual(pt.get("k5.k.k.n"), ptree.Tree(None))

        self.assertRaises(ptree.BadPathError, pt.get, "non.existent.path")

    def test_get_set_attribute(self):
        pt = ptree.Tree()

        # __setattribute__
        pt.k       = 1
        pt.k.k     = 2.5
        pt.k.k.k   = "ala ma kota"
        pt.k.k.k.f = False
        pt.k.k.k.t = True
        pt.k.k.k.n = None

        # __getattribute__
        self.assertEqual(pt.k,       1)
        self.assertEqual(pt.k.k,     2.5)
        self.assertEqual(pt.k.k.k,   "ala ma kota")
        self.assertEqual(pt.k.k.k.f, False)
        self.assertEqual(pt.k.k.k.t, True)
        self.assertEqual(pt.k.k.k.n, None)
        self.assertEqual(pt.k.k.k.n, ptree.Tree(None))

        with self.assertRaises(AttributeError):
            pt.non.existent.path

    def test_count(self):
        pt = ptree.Tree()
        pt.add("k1", ptree.Tree())
        pt.add("k2", ptree.Tree())
        pt.add("k1", ptree.Tree())
        pt.add("k3", ptree.Tree())
        pt.add("k1", ptree.Tree())
        pt.add("k2", ptree.Tree())

        self.assertEqual(pt.count("k1"), 3)
        self.assertEqual(pt.count("k2"), 2)
        self.assertEqual(pt.count("k3"), 1)

    def test_search(self):
        pt = ptree.Tree()
        pt.add("k1", ptree.Tree())
        pt.add("k2", ptree.Tree())
        pt.add("k1", ptree.Tree())
        pt.add("k3", ptree.Tree())
        pt.add("k1", ptree.Tree())
        pt.add("k2", ptree.Tree())

        self.assertEqual(sum(1 for i in pt.search("k1")), 3)
        self.assertEqual(sum(1 for i in pt.search("k2")), 2)
        self.assertEqual(sum(1 for i in pt.search("k3")), 1)

    def test_ptree_bad_data(self):
        pt = ptree.Tree("non-convertible string")

        self.assertRaises(ptree.BadDataError, int,   pt)
        self.assertRaises(ptree.BadDataError, float, pt)
        self.assertRaises(ptree.BadDataError, bool,  pt)

    def test_bool(self):
        pt = ptree.Tree()
        pt.add("bool.false",   "false")
        pt.add("bool.false",   False)
        pt.add("bool.false",   "0")
        pt.add("bool.false",   0)
        pt.add("bool.true",    "true")
        pt.add("bool.true",    True)
        pt.add("bool.true",    "1")
        pt.add("bool.true",    1)
        pt.add("bool.invalid", "")
        pt.add("bool.invalid", "tt")
        pt.add("bool.invalid", "ff")
        pt.add("bool.invalid", 2)
        pt.add("bool.invalid", -1)

        for k, v in pt.bool.search("false"):
            self.assertEqual(v, False)

        for k, v in pt.bool.search("true"):
            self.assertEqual(v, True)

        for k, v in pt.bool.search("invalid"):
            with self.assertRaises(ptree.BadDataError):
                v == True

            with self.assertRaises(ptree.BadDataError):
                v == False

    def test_sort(self):
        pt = ptree.Tree()
        pt.put("one",   "1")
        pt.put("two",   "2")
        pt.put("three", "3")
        pt.put("four",  "4")

        # sort by key
        pt.sort()

        self.assertEqual(pt.index("four"),  0)
        self.assertEqual(pt[0], 4)

        self.assertEqual(pt.index("one"),   1)
        self.assertEqual(pt[1], 1)

        self.assertEqual(pt.index("three"), 2)
        self.assertEqual(pt[2], 3)

        self.assertEqual(pt.index("two"),   3)
        self.assertEqual(pt[3], 2)

        # sort by value
        pt.sort(lambda lhs, rhs: int(lhs[1]) < int(rhs[1]))

        self.assertEqual(pt.index("one"),   0)
        self.assertEqual(pt[0], 1)

        self.assertEqual(pt.index("two"),   1)
        self.assertEqual(pt[1], 2)

        self.assertEqual(pt.index("three"), 2)
        self.assertEqual(pt[2], 3)

        self.assertEqual(pt.index("four"),  3)
        self.assertEqual(pt[3], 4)


if __name__ == '__main__':
    unittest.main()


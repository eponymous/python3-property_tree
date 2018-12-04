from distutils.core import setup, Extension

setup(name='property_tree', ext_modules=[Extension('property_tree', ['property_tree.cpp'])])


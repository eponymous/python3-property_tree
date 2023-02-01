#################################################################################
#                                                                               #
#  This file is part of ptree distributed under                                 #
#  the terms of the Boost Software License - Version 1.0.                       #
#  See the accompanying  LICENSE file or <http://www.boost.org/LICENSE_1_0.txt> #
#                                                                               #
#################################################################################

from distutils.core import setup, Extension


setup(
    name="proptree",
    version="1.0",
    description="Python bindings for C++ boost::property_tree",
    author="Dan Eicher",
    url="https://github.com/eponymous/python3-property_tree",
    ext_modules=[Extension("proptree", ["proptree/proptree.cpp"])],
    test_suite="test",
    classifiers=[
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: Boost Software License 1.0 (BSL-1.0)",
        "Operating System :: OS Independent",
        "Programming Language :: C++",
        "Programming Language :: Python",
    ],
)

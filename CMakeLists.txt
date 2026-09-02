cmake_minimum_required(VERSION 3.15...4.4)

set(FASTNUMPARSE_PROJECT_NAME "fastnumparse")
set(FASTNUMPARSE_PROJECT_VERSION "0.1.0")

# scikit-build-core defines these variables when building a Python package.
# Keep standalone CMake configuration usable for IDEs and local development.
if(DEFINED SKBUILD_PROJECT_NAME)
    set(FASTNUMPARSE_PROJECT_NAME "${SKBUILD_PROJECT_NAME}")
endif()
if(DEFINED SKBUILD_PROJECT_VERSION)
    set(FASTNUMPARSE_PROJECT_VERSION "${SKBUILD_PROJECT_VERSION}")
endif()

project(
    ${FASTNUMPARSE_PROJECT_NAME}
    VERSION ${FASTNUMPARSE_PROJECT_VERSION}
    LANGUAGES CXX
)

set(PYBIND11_FINDPYTHON ON)
add_subdirectory(dep/fast_float EXCLUDE_FROM_ALL)
add_subdirectory(dep/nanothread EXCLUDE_FROM_ALL)
add_subdirectory(dep/pybind11/2.13.6 EXCLUDE_FROM_ALL)

pybind11_add_module(
    fastnumparse
    MODULE
    src/fastnumparse/bindings.cpp
    src/fastnumparse/fastnumparse.cpp
)
target_compile_features(fastnumparse PRIVATE cxx_std_17)
target_compile_definitions(
    fastnumparse
    PRIVATE FASTNUMPARSE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(fastnumparse PRIVATE fast_float nanothread)

install(TARGETS fastnumparse LIBRARY DESTINATION fastnumparse)

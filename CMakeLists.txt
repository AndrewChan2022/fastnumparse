cmake_minimum_required(VERSION 3.15...4.4)

set(FASTNUMPARSE_PROJECT_NAME "fastnumparse")
set(FASTNUMPARSE_PROJECT_VERSION "0.0.1")

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

set(
    FASTNUMPARSE_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

set(PYBIND11_FINDPYTHON ON)
set(NANOTHREAD_STATIC ON)
add_subdirectory(dep/fast_float EXCLUDE_FROM_ALL)
add_subdirectory(dep/nanothread EXCLUDE_FROM_ALL)
add_subdirectory(dep/pybind11/2.13.6 EXCLUDE_FROM_ALL)

add_subdirectory(src/fastnumparse)

set(FASTNUMPARSE_BUILD_TESTAPP_DEFAULT ON)
if(SKBUILD)
    set(FASTNUMPARSE_BUILD_TESTAPP_DEFAULT OFF)
endif()

option(
    FASTNUMPARSE_BUILD_TESTAPP
    "Build the fastnumparse C++ test application"
    ${FASTNUMPARSE_BUILD_TESTAPP_DEFAULT}
)

if(FASTNUMPARSE_BUILD_TESTAPP)
    enable_testing()
    add_subdirectory(testapp)
endif()

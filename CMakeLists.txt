cmake_minimum_required(VERSION 3.15...4.4)

project(${SKBUILD_PROJECT_NAME} VERSION ${SKBUILD_PROJECT_VERSION} LANGUAGES CXX)

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_core MODULE src/fastnumparse/_core.cpp)
target_compile_features(_core PRIVATE cxx_std_17)
target_compile_definitions(
    _core
    PRIVATE FASTNUMPARSE_VERSION="${SKBUILD_PROJECT_VERSION}"
)

install(TARGETS _core LIBRARY DESTINATION fastnumparse)

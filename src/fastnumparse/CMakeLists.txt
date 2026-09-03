pybind11_add_module(
    _fastnumparse
    SHARED
    fastnumparse.cpp
)

target_compile_features(_fastnumparse PRIVATE cxx_std_17)
target_compile_definitions(
    _fastnumparse
    PRIVATE
        FAST_NUM_PARSE_EXPORTS
        FASTNUMPARSE_VERSION="${PROJECT_VERSION}"
)
target_include_directories(
    _fastnumparse
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(_fastnumparse PRIVATE fast_float nanothread)

install(
    TARGETS _fastnumparse
    LIBRARY DESTINATION fastnumparse
    RUNTIME DESTINATION fastnumparse
)

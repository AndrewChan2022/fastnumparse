add_executable(
    fastnumparse_testapp
    main.cpp
    ${PROJECT_SOURCE_DIR}/src/fastnumparse/fastnumparse.cpp
)

target_compile_features(fastnumparse_testapp PRIVATE cxx_std_17)
target_compile_definitions(
    fastnumparse_testapp
    PRIVATE
        FAST_NUM_PARSE_EXPORTS
        FASTNUMPARSE_VERSION="${PROJECT_VERSION}"
        FASTNUMPARSE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)
target_include_directories(
    fastnumparse_testapp
    PRIVATE ${PROJECT_SOURCE_DIR}/src/fastnumparse
)
target_link_libraries(
    fastnumparse_testapp
    PRIVATE
        pybind11::embed
        fast_float
        nanothread
)
add_dependencies(fastnumparse_testapp _fastnumparse)
set_target_properties(
    fastnumparse_testapp
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FASTNUMPARSE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME fastnumparse_cpp_test COMMAND fastnumparse_testapp)

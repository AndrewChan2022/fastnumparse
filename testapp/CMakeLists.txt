add_executable(fastnumparse_testapp main.cpp)

target_compile_features(fastnumparse_testapp PRIVATE cxx_std_17)
target_compile_definitions(
    fastnumparse_testapp
    PRIVATE FASTNUMPARSE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)
target_link_libraries(fastnumparse_testapp PRIVATE _fastnumparse)
set_target_properties(
    fastnumparse_testapp
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FASTNUMPARSE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_test(NAME fastnumparse_cpp_test COMMAND fastnumparse_testapp)

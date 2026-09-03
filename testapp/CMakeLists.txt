add_executable(fastnumparse_testapp main.cpp)

target_compile_features(fastnumparse_testapp PRIVATE cxx_std_17)
target_compile_definitions(
    fastnumparse_testapp
    PRIVATE FASTNUMPARSE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
)
target_link_libraries(fastnumparse_testapp PRIVATE _fastnumparse)

if(WIN32)
    add_custom_command(
        TARGET fastnumparse_testapp
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:_fastnumparse>
                $<TARGET_FILE_DIR:fastnumparse_testapp>
        VERBATIM
    )
endif()

add_test(NAME fastnumparse_cpp_test COMMAND fastnumparse_testapp)

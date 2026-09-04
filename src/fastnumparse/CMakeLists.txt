pybind11_add_module(
    _fastnumparse
    MODULE
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
set_target_properties(
    _fastnumparse
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FASTNUMPARSE_RUNTIME_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${FASTNUMPARSE_RUNTIME_OUTPUT_DIRECTORY}"
)

# For standalone Visual Studio builds, update the extension imported by the
# active Python environment. Scikit-build editable installs map the native
# module directly to this site-packages location.
if(NOT SKBUILD)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c
                "import sysconfig; print(sysconfig.get_path('platlib'), end='')"
        OUTPUT_VARIABLE FASTNUMPARSE_PYTHON_PLATLIB
        COMMAND_ERROR_IS_FATAL ANY
    )
    set(
        FASTNUMPARSE_PYTHON_PACKAGE_DIR
        "${FASTNUMPARSE_PYTHON_PLATLIB}/fastnumparse"
    )

    add_custom_command(
        TARGET _fastnumparse
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${FASTNUMPARSE_PYTHON_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:_fastnumparse>
                "${FASTNUMPARSE_PYTHON_PACKAGE_DIR}"
        VERBATIM
    )
endif()

install(
    TARGETS _fastnumparse
    LIBRARY DESTINATION fastnumparse
    RUNTIME DESTINATION fastnumparse
)

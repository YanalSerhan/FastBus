# Format.cmake — `format` and `format-check` targets.
#
# Both delegate to scripts/run_clang_format.py so CI, pre-commit and CMake share one file list
# and one clang-format invocation.

find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    set(_fmt_script "${PROJECT_SOURCE_DIR}/scripts/run_clang_format.py")
    add_custom_target(format
        COMMAND Python3::Interpreter "${_fmt_script}" --fix
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Formatting sources with clang-format"
        VERBATIM)
    add_custom_target(format-check
        COMMAND Python3::Interpreter "${_fmt_script}" --check
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking source formatting with clang-format"
        VERBATIM)
else()
    message(STATUS "fastbus: Python 3 not found, `format`/`format-check` targets unavailable")
endif()

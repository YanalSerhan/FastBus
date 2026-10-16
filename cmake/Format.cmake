# Format.cmake — `format` and `format-check` targets.
#
# Both delegate to scripts/run_clang_format.py so CI, pre-commit, dev.ps1 and CMake share one
# file list and one clang-format invocation. When uv is available the script runs through
# `uv run`, which provides the pinned clang-format 18 from uv.lock; otherwise a plain Python 3
# is used and the script looks for clang-format on the system.

find_program(FASTBUS_UV uv)
set(_fmt_script "${PROJECT_SOURCE_DIR}/scripts/run_clang_format.py")

if(FASTBUS_UV)
    set(_fmt_python "${FASTBUS_UV}" run --frozen --project "${PROJECT_SOURCE_DIR}" python)
else()
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(_fmt_python "${Python3_EXECUTABLE}")
    endif()
endif()

if(_fmt_python)
    add_custom_target(format
        COMMAND ${_fmt_python} "${_fmt_script}" --fix
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Formatting sources with clang-format"
        VERBATIM)
    add_custom_target(format-check
        COMMAND ${_fmt_python} "${_fmt_script}" --check
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Checking source formatting with clang-format"
        VERBATIM)
else()
    message(STATUS "fastbus: neither uv nor Python 3 found, `format`/`format-check` unavailable")
endif()

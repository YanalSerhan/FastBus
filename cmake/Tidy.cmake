# Tidy.cmake — optional clang-tidy integration.
#
# With FASTBUS_ENABLE_TIDY=ON every fastbus target gets CXX_CLANG_TIDY set, so clang-tidy runs
# as part of the normal build using the checked-in .clang-tidy (WarningsAsErrors: '*').
# Works with Ninja/Makefile generators (Linux CI, `windows-clangcl` locally). The Visual Studio
# generator ignores CXX_CLANG_TIDY — use the `windows-clangcl` preset on Windows.

if(FASTBUS_ENABLE_TIDY)
    find_program(FASTBUS_CLANG_TIDY NAMES clang-tidy clang-tidy-18 clang-tidy-19 clang-tidy-20)
    if(NOT FASTBUS_CLANG_TIDY)
        message(FATAL_ERROR "FASTBUS_ENABLE_TIDY is ON but clang-tidy was not found")
    endif()
    message(STATUS "fastbus: clang-tidy enabled (${FASTBUS_CLANG_TIDY})")
endif()

# Applies clang-tidy to one target (no-op when tidy is disabled).
function(fastbus_enable_tidy target)
    if(FASTBUS_ENABLE_TIDY)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${FASTBUS_CLANG_TIDY}")
    endif()
endfunction()

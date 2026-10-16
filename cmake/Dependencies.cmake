# Dependencies.cmake — third-party code via FetchContent, pinned by tag AND commit SHA.
#
# Pins are mirrored in docs/versions.md. When bumping one, update both places.
# Dependencies are fetched lazily (only when the part of the build that needs them is enabled),
# and their include directories are marked SYSTEM so their headers never trigger our warnings.
# (FetchContent's own SYSTEM keyword needs CMake 3.25; we support 3.24, so we do it by hand.)

include(FetchContent)

# GoogleTest v1.17.0
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723 # v1.17.0
)

# Google Benchmark v1.9.4
FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        eddb0241389718a23a42db6af5f0164b6e0139af # v1.9.4
)

# CLI11 v2.7.2 (tools, from M3)
FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        cbd58a3696887b34c70949aef21a71735a0c2ad5 # v2.7.2
)

# nlohmann_json v3.12.0 (versioned config files, from M3)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        55f93686c01528224f448c19128836e7df245f72 # v3.12.0
)

# Eigen (EKF, M7) is pinned when M7 starts; see docs/versions.md.

# Marks the interface include directories of the given targets as SYSTEM.
function(fastbus_mark_system)
    foreach(tgt IN LISTS ARGN)
        if(NOT TARGET ${tgt})
            continue()
        endif()
        get_target_property(_aliased ${tgt} ALIASED_TARGET)
        if(_aliased)
            set(tgt ${_aliased})
        endif()
        get_target_property(_dirs ${tgt} INTERFACE_INCLUDE_DIRECTORIES)
        if(_dirs)
            set_target_properties(${tgt} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_dirs}")
        endif()
    endforeach()
endfunction()

macro(fastbus_fetch_googletest)
    if(NOT TARGET GTest::gtest)
        # Match the MSVC runtime CMake picks for our targets (/MD[d]).
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        fastbus_mark_system(gtest gtest_main gmock gmock_main)
        include(GoogleTest)
    endif()
endmacro()

macro(fastbus_fetch_benchmark)
    if(NOT TARGET benchmark::benchmark)
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(benchmark)
        fastbus_mark_system(benchmark benchmark_main)
    endif()
endmacro()

macro(fastbus_fetch_cli11)
    if(NOT TARGET CLI11::CLI11)
        FetchContent_MakeAvailable(cli11)
        fastbus_mark_system(CLI11)
    endif()
endmacro()

macro(fastbus_fetch_json)
    if(NOT TARGET nlohmann_json::nlohmann_json)
        set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
        set(JSON_Install OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(nlohmann_json)
        fastbus_mark_system(nlohmann_json)
    endif()
endmacro()

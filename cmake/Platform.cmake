# Platform.cmake — build-time platform selection (ADR-002).
#
# Core logic contains no #ifdef for the OS. Instead, the build picks one backend directory:
#   src/platform/windows/*.cpp   on Windows
#   src/platform/posix/*.cpp     everywhere else (Linux is the supported POSIX target)
# and the system libraries each backend needs are collected in `fastbus::platform_libs`.

find_package(Threads REQUIRED)

add_library(fastbus_platform_libs INTERFACE)
add_library(fastbus::platform_libs ALIAS fastbus_platform_libs)
target_link_libraries(fastbus_platform_libs INTERFACE Threads::Threads)

if(WIN32)
    set(FASTBUS_PLATFORM "windows")
    # ws2_32: Winsock for the UDP transport (M6). winmm: timeBeginPeriod (HighResolutionTimerScope).
    # WIN32_LEAN_AND_MEAN/NOMINMAX keep <windows.h> sane.
    target_link_libraries(fastbus_platform_libs INTERFACE ws2_32 winmm)
    target_compile_definitions(fastbus_platform_libs INTERFACE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
else()
    set(FASTBUS_PLATFORM "posix")
    # Older glibc (< 2.34) keeps shm_open in librt; newer glibc folds it into libc.
    include(CheckLibraryExists)
    check_library_exists(rt shm_open "" FASTBUS_HAVE_LIBRT)
    if(FASTBUS_HAVE_LIBRT)
        target_link_libraries(fastbus_platform_libs INTERFACE rt)
    endif()
    target_link_libraries(fastbus_platform_libs INTERFACE ${CMAKE_DL_LIBS})
endif()

set(FASTBUS_PLATFORM_SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/platform/${FASTBUS_PLATFORM}")

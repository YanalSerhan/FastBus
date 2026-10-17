# fastbus::core — strong types, time, Result/Error, SPSC/MPSC rings, CRC32C.
# Most of core is header-only templates; the .cpp files hold the few non-template pieces.
fastbus_add_library(core
    SOURCES
        assert.cpp
        build_info.cpp
        error.cpp
        sleep.cpp
    PUBLIC_DEPS
        fastbus::platform)

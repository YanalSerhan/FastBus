# fastbus::core — strong types, Result<T,E>, SPSC/MPSC rings, CRC32C (from M1).
fastbus_add_library(core
    SOURCES
        build_info.cpp
    PUBLIC_DEPS
        fastbus::platform)

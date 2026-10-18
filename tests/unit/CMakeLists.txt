fastbus_add_test(fastbus_unit_version
    SOURCES version_test.cpp
    DEPS fastbus::core
    LABELS unit)

fastbus_add_test(fastbus_unit_core_types
    SOURCES
        strong_type_test.cpp
        time_test.cpp
        result_test.cpp
    DEPS fastbus::core
    LABELS unit)

fastbus_add_test(fastbus_unit_clock
    SOURCES clock_test.cpp
    DEPS fastbus::core
    LABELS unit
    TIMEOUT 120)

fastbus_add_test(fastbus_unit_thread
    SOURCES thread_test.cpp
    DEPS fastbus::platform
    LABELS unit)

fastbus_add_test(fastbus_unit_spsc_ring_layout
    SOURCES spsc_ring_layout_test.cpp
    DEPS fastbus::core
    LABELS unit)

fastbus_add_test(fastbus_unit_spsc_ring
    SOURCES
        spsc_ring_test.cpp
        spsc_ring_wrap_test.cpp
    DEPS fastbus::core
    LABELS unit
    TIMEOUT 120)

# M2.1/M2.2: shared memory and processes, including real child processes (fb_test_child).
fastbus_add_test(fastbus_unit_ipc
    SOURCES
        process_test.cpp
        shared_memory_test.cpp
    DEPS fastbus::platform
    LABELS unit
    TIMEOUT 120)
fastbus_use_test_child(fastbus_unit_ipc)

# M2.3/M2.4: OffsetPtr, CRC32C (platform) and the message header (core).
fastbus_add_test(fastbus_unit_message
    SOURCES
        crc32c_test.cpp
        message_header_test.cpp
        offset_ptr_test.cpp
    DEPS fastbus::core
    LABELS unit)

# M2.5: slab block pool.
fastbus_add_test(fastbus_unit_slab
    SOURCES slab_test.cpp
    DEPS fastbus::core
    LABELS unit)

# M2.3: transport segments, including killed child processes (fb_test_child).
fastbus_add_test(fastbus_unit_segment
    SOURCES segment_test.cpp
    DEPS fastbus::transport
    LABELS unit
    TIMEOUT 120)
fastbus_use_test_child(fastbus_unit_segment)

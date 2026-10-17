# Allocation tests (label: alloc). Each binary replaces the global operator new/delete, which is
# process-wide, so every component gets its own executable.
fastbus_add_test(fastbus_alloc_spsc_ring
    SOURCES spsc_ring_noalloc_test.cpp
    DEPS fastbus::core
    LABELS alloc
    TIMEOUT 120)

# Stress tests (label: stress). Long-running multi-threaded tests; each test case is its own
# ctest entry with a timeout, and is also bounded internally by a time limit.
set(FASTBUS_STRESS_MULTIPLIER "1" CACHE STRING
    "Multiplier for stress-test item counts (e.g. 10 for a local soak run)")

fastbus_add_test(fastbus_stress_spsc_ring
    SOURCES spsc_ring_stress_test.cpp
    DEPS fastbus::core
    LABELS stress
    TIMEOUT 120)

target_compile_definitions(fastbus_stress_spsc_ring PRIVATE
    FASTBUS_STRESS_MULTIPLIER=${FASTBUS_STRESS_MULTIPLIER})
# Sanitizers slow code down 5-20x and coverage counters (atomic) slow contended code further:
# run 1/10 of the items there. The protocol is the same; only the iteration count changes.
if(FASTBUS_ACTIVE_SANITIZERS OR FASTBUS_COVERAGE)
    target_compile_definitions(fastbus_stress_spsc_ring PRIVATE FASTBUS_STRESS_REDUCED=1)
endif()

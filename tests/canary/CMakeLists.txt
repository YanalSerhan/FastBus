# Sanitizer canaries: each program contains one deliberate bug the matching sanitizer must
# catch. If a sanitizer silently stops working (wrong flags, wrong preset), its canary turns red.
#
# Pass criterion: the sanitizer's own report text appears in the output (PASS_REGULAR_EXPRESSION).
# This is stricter than plain WILL_FAIL, which would also "pass" on an unrelated crash.
#
# These are plain executables (not GoogleTest) so a sanitizer abort cannot be mistaken for a
# test-framework failure, and they deliberately do NOT link fastbus::warnings.

function(_fastbus_add_canary name source expected_report)
    add_executable(${name} ${source})
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        PASS_REGULAR_EXPRESSION "${expected_report}"
        LABELS canary
        TIMEOUT 60)
endfunction()

if("address" IN_LIST FASTBUS_ACTIVE_SANITIZERS)
    _fastbus_add_canary(canary_asan_heap_overflow asan_heap_overflow.cpp
        "AddressSanitizer: heap-buffer-overflow")
endif()
if("undefined" IN_LIST FASTBUS_ACTIVE_SANITIZERS)
    _fastbus_add_canary(canary_ubsan_signed_overflow ubsan_signed_overflow.cpp
        "runtime error: signed integer overflow")
endif()
if("thread" IN_LIST FASTBUS_ACTIVE_SANITIZERS)
    _fastbus_add_canary(canary_tsan_data_race tsan_data_race.cpp
        "ThreadSanitizer: data race")
endif()

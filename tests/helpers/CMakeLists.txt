# Helper programs started by cross-process tests (M2.2+). Tests find them through the
# compile definition FASTBUS_TEST_CHILD (absolute path, set by fastbus_use_test_child()).
fastbus_add_executable(fb_test_child
    SOURCES fb_test_child.cpp
    DEPS fastbus::platform)

# Gives `target` the helper's path as FASTBUS_TEST_CHILD and builds the helper first.
function(fastbus_use_test_child target)
    add_dependencies(${target} fb_test_child)
    target_compile_definitions(${target} PRIVATE
        FASTBUS_TEST_CHILD="$<TARGET_FILE:fb_test_child>")
endfunction()

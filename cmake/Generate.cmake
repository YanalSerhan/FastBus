# Generate.cmake — IDL code generation (placeholder, implemented in M4).
#
# Planned interface:
#   fastbus_generate_messages(TARGET <tgt> IDL <files...>)
# runs tools/fbgen via add_custom_command with DEPENDS on fbgen + the .fbidl inputs, writes
# headers into the build tree and adds that directory to <tgt>'s include path.
# Must work with both the Visual Studio generator and Ninja.

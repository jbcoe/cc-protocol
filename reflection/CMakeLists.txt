xyz_add_library(
  NAME reflection_protocol
  ALIAS xyz_protocol::reflection_protocol)
target_sources(
  reflection_protocol INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol.h>)

xyz_add_test(
  NAME
  reflection_protocol_test
  VERSION
  26
  LINK_LIBRARIES
  xyz_protocol::reflection_protocol
  FILES
  protocol_test.cc)
target_compile_options(reflection_protocol_test PRIVATE -freflection)

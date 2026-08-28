xyz_add_library(
  NAME reflection_protocol
  ALIAS xyz_protocol::reflection_protocol)
target_sources(
  reflection_protocol INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol.hh>)

xyz_add_test(
  NAME
  reflection_protocol_test
  VERSION
  26
  LINK_LIBRARIES
  xyz_protocol::reflection_protocol
  FILES
  protocol_test.cc
  allocator_tests.cc
  consteval_check_test.cc)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(reflection_protocol_test PRIVATE -freflection)
endif()

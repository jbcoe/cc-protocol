cmake_minimum_required(VERSION 3.22.4 FATAL_ERROR)
cmake_policy(SET CMP0127 NEW)
cmake_policy(SET CMP0135 NEW)
cmake_policy(SET CMP0140 NEW)

if(NOT DEFINED PROJECT_NAME)
  set(XYZ_PROTOCOL_IS_NOT_SUBPROJECT ON)
endif()

set(XYZ_PROTOCOL_VERSION 0.0.1)

project(
  protocol
  LANGUAGES CXX
  VERSION ${XYZ_PROTOCOL_VERSION})

include(CTest)
include(FetchContent)
include(GNUInstallDirs)
include(CMakeDependentOption)
include(CMakePackageConfigHelpers)

option(ENABLE_SANITIZERS
       "Enable Address Sanitizer and Undefined Behaviour Sanitizer if available"
       OFF)

option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behaviour Sanitizer" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_MSAN "Enable Memory Sanitizer" OFF)

option(
  XYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL
  "Also build and test tutorials/reflection.cc, which requires a compiler \
with C++26 P2996 reflection support (GCC 16+ with -freflection)."
  OFF)

if(XYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-std=c++26 -freflection")
  check_cxx_source_compiles(
    "
    #include <meta>
    constexpr std::meta::info reflection_of_int = ^^int;
    int main() {}
    "
    XYZ_PROTOCOL_REFLECTION_SUPPORTED)
  unset(CMAKE_REQUIRED_FLAGS)
  if(NOT XYZ_PROTOCOL_REFLECTION_SUPPORTED)
    message(
      FATAL_ERROR
        "XYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL is ON but the compiler "
        "(${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}) does not "
        "accept '-std=c++26 -freflection'. C++26 reflection currently "
        "requires GCC 16 or newer; configure with e.g. "
        "CXX=g++-16 CC=gcc-16 and a separate build directory (-B).")
  endif()
endif()

if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN OR ENABLE_MSAN)
  set(ENABLE_SANITIZERS ON)
endif()

# Set up Python discovery
set(Python3_FIND_VIRTUALENV FIRST)
if(DEFINED ENV{UV_PROJECT_ENVIRONMENT})
  set(Python3_ROOT_DIR "$ENV{UV_PROJECT_ENVIRONMENT}")
elseif(DEFINED ENV{VIRTUAL_ENV})
  set(Python3_ROOT_DIR "$ENV{VIRTUAL_ENV}")
elseif(EXISTS "${PROJECT_SOURCE_DIR}/.venv")
  set(Python3_ROOT_DIR "${PROJECT_SOURCE_DIR}/.venv")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Verify required modules
execute_process(
  COMMAND ${Python3_EXECUTABLE} -c
          "import jinja2; import clang; import xyz.cppmodel"
  RESULT_VARIABLE PYTHON_MODULES_FOUND
  ERROR_QUIET)
if(NOT PYTHON_MODULES_FOUND EQUAL 0)
  message(
    FATAL_ERROR
      "Python interpreter missing required modules (jinja2, clang, xyz.cppmodel). "
      "Please run 'uv sync' or install them manually.")
endif()

# Include necessary submodules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${PROJECT_SOURCE_DIR}/cmake/modules)

# Custom CMake Includes
include(xyz_add_library)
include(xyz_add_test)
include(xyz_add_object_library)
include(xyz_generate_protocol)

find_package(ClangTidy)
find_package(IWYU)
xyz_add_library(
  NAME protocol
  ALIAS xyz_protocol::protocol
  FILES protocol.cc)
target_sources(
  protocol PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol.h>)

if(XYZ_PROTOCOL_IS_NOT_SUBPROJECT)

  if(${BUILD_TESTING})
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2)
    FetchContent_Declare(
      google_benchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG v1.9.1)
    # For Windows: Prevent overriding the parent project's compiler/linker
    # settings
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest google_benchmark)

    enable_testing()

    xyz_generate_protocol(
      CLASS_NAME A INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_A.h
      HEADER interface_A.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A.h)
    xyz_generate_protocol(
      CLASS_NAME A_Subset INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_A_Subset.h
      HEADER interface_A_Subset.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A_Subset.h)
    xyz_generate_protocol(
      CLASS_NAME B INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_B.h
      HEADER interface_B.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_B.h)
    xyz_generate_protocol(
      CLASS_NAME C INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_C.h
      HEADER interface_C.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_C.h)
    xyz_generate_protocol(
      CLASS_NAME D INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_D.h
      HEADER interface_D.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_D.h)

    add_custom_target(
      generate_protocols
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A.h
              ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A_Subset.h
              ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_B.h
              ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_C.h
              ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_D.h)

    xyz_add_test(
      NAME
      protocol_test
      LINK_LIBRARIES
      protocol
      FILES
      protocol_test.cc
      interface_A.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A.h
      interface_A_Subset.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A_Subset.h
      interface_B.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_B.h
      interface_C.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_C.h
      interface_D.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_D.h)
    add_dependencies(protocol_test generate_protocols)
    target_include_directories(protocol_test
                               PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    # Standalone teaching tutorials (tutorials/) -- see tutorials/README.md.
    # Each is self-contained, with no dependency on protocol.h.
    xyz_add_test(
      NAME
      type_erasure_tutorial
      FILES
      tutorials/type_erasure.cc)

    xyz_add_test(
      NAME
      vanishing_this_pointer_tutorial
      FILES
      tutorials/vanishing_this_pointer.cc)

    if(XYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL)
      xyz_add_test(
        NAME
        reflection_tutorial
        VERSION
        26
        FILES
        tutorials/reflection.cc)
      target_compile_options(reflection_tutorial PRIVATE -freflection)
    endif()

    add_executable(protocol_benchmark protocol_benchmark.cc)
    target_link_libraries(protocol_benchmark PRIVATE protocol benchmark::benchmark)
    add_dependencies(protocol_benchmark generate_protocols)
    target_include_directories(protocol_benchmark PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

    add_custom_target(run_benchmark
      COMMAND protocol_benchmark
      DEPENDS protocol_benchmark
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      USES_TERMINAL
    )

    if(NOT MSVC)
      add_test(
        NAME concept_errors_test
        COMMAND
          ${Python3_EXECUTABLE} -m pytest
          ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_concept_errors.py
          --compiler=${CMAKE_CXX_COMPILER} --flags=-std=c++20
          --flags=-I${CMAKE_CURRENT_BINARY_DIR}
          --flags=-I${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
  endif(${BUILD_TESTING})

endif()

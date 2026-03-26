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
xyz_add_library(NAME protocol ALIAS xyz_protocol::protocol)
target_sources(
  protocol INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol.h>)

xyz_add_object_library(NAME protocol_cc FILES protocol.cc LINK_LIBRARIES
                       xyz_protocol::protocol)

if(XYZ_PROTOCOL_IS_NOT_SUBPROJECT)

  if(${BUILD_TESTING})
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2)
    # For Windows: Prevent overriding the parent project's compiler/linker
    # settings
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    option(XYZ_PROTOCOL_GENERATE_MANUAL_VTABLE "Generate manual vtable template" OFF)

    set(OPT_MANUAL_VTABLE "")
    if(XYZ_PROTOCOL_GENERATE_MANUAL_VTABLE)
      set(OPT_MANUAL_VTABLE MANUAL_VTABLE)
    endif()

    xyz_generate_protocol(
      CLASS_NAME A INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_A.h
      HEADER interface_A.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A.h ${OPT_MANUAL_VTABLE})
    xyz_generate_protocol(
      CLASS_NAME B INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_B.h
      HEADER interface_B.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_B.h ${OPT_MANUAL_VTABLE})
    xyz_generate_protocol(
      CLASS_NAME C INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/interface_C.h
      HEADER interface_C.h
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_C.h ${OPT_MANUAL_VTABLE})

    add_custom_target(
      generate_protocols
      DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A.h
              ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_B.h
              ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_C.h)

    xyz_add_test(
      NAME
      protocol_test
      LINK_LIBRARIES
      protocol
      FILES
      protocol_test.cc
      interface_A.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_A.h
      interface_B.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_B.h
      interface_C.h
      ${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_C.h)
    add_dependencies(protocol_test generate_protocols)
    target_include_directories(protocol_test
                               PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

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

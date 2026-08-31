cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
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
option(ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)

# The implementation requires C++26 reflection (P2996). GCC only enables it
# with -freflection, which it accepts solely alongside -std=c++26, so every
# target in the build is compiled as C++26.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-freflection)
  set(CMAKE_REQUIRED_FLAGS "-freflection")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Clang means the clang-p2996 fork (https://github.com/bloomberg/clang-p2996),
  # the only Clang with P2996 support; upstream Clang cannot parse the
  # sources. The fork enables reflection with -freflection-latest, expansion
  # statements separately, and provides <meta> only in its bundled libc++.
  # data_member_options::no_unique_address is deprecated there in favour of a
  # fork-specific attributes member that GCC does not have, so the deprecation
  # warning is disabled rather than moving off the standard field.
  add_compile_options(-freflection-latest -fexpansion-statements -stdlib=libc++
                      -Wno-deprecated-declarations)
  add_link_options(-stdlib=libc++)
  set(CMAKE_REQUIRED_FLAGS
      "-freflection-latest -fexpansion-statements -stdlib=libc++")
endif()
include(CheckCXXSourceCompiles)
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
      "protocol requires C++26 reflection, which the compiler "
      "(${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}) does not "
      "support. Use a GCC trunk snapshot (https://jwakely.github.io/pkg-gcc-latest/) "
      "or gcc-16; scripts/cmake.sh selects one when CXX is unset.")
endif()

# A non-distro GCC (e.g. the gcc-latest trunk snapshot installed to
# /opt/gcc-latest, see docker/Dockerfile) keeps its libstdc++ in a
# directory ld.so does not search by default. Embed that directory as a
# build rpath so test executables linked against it run without needing
# LD_LIBRARY_PATH set.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.so
    OUTPUT_VARIABLE XYZ_PROTOCOL_LIBSTDCXX_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(XYZ_PROTOCOL_LIBSTDCXX_PATH
                          "${XYZ_PROTOCOL_LIBSTDCXX_PATH}" REALPATH)
  get_filename_component(XYZ_PROTOCOL_LIBSTDCXX_DIRECTORY
                          "${XYZ_PROTOCOL_LIBSTDCXX_PATH}" DIRECTORY)
  list(APPEND CMAKE_BUILD_RPATH "${XYZ_PROTOCOL_LIBSTDCXX_DIRECTORY}")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # Same for the clang-p2996 fork's libc++, which lives in the fork's build
  # tree rather than a directory ld.so searches.
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libc++.so
    OUTPUT_VARIABLE XYZ_PROTOCOL_LIBCXX_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(XYZ_PROTOCOL_LIBCXX_PATH
                          "${XYZ_PROTOCOL_LIBCXX_PATH}" REALPATH)
  get_filename_component(XYZ_PROTOCOL_LIBCXX_DIRECTORY
                          "${XYZ_PROTOCOL_LIBCXX_PATH}" DIRECTORY)
  list(APPEND CMAKE_BUILD_RPATH "${XYZ_PROTOCOL_LIBCXX_DIRECTORY}")
endif()

if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
  set(ENABLE_SANITIZERS ON)
endif()

# Runtime coverage only: consteval code never reaches the gcov
# instrumentation, see scripts/consteval_coverage.py.
if(ENABLE_COVERAGE)
  add_compile_options(--coverage)
  add_link_options(--coverage)
endif()

# Include necessary submodules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${PROJECT_SOURCE_DIR}/cmake/modules)

# Custom CMake Includes
include(xyz_add_library)
include(xyz_add_test)
include(xyz_add_object_library)

find_package(ClangTidy)

xyz_add_library(
  NAME protocol
  ALIAS xyz_protocol::protocol)
target_sources(
  protocol
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol.hh>
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/name_mangling.h>)

if(XYZ_PROTOCOL_IS_NOT_SUBPROJECT)

  if(${BUILD_TESTING})
    # SYSTEM marks the dependencies' include directories as system headers so
    # that clang-tidy and compiler warnings do not report on third-party code.
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.18.0
      SYSTEM)
    # For Windows: Prevent overriding the parent project's compiler/linker
    # settings
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    xyz_add_test(
      NAME
      protocol_test
      LINK_LIBRARIES
      xyz_protocol::protocol
      FILES
      protocol_test.cc
      allocator_tests.cc
      consteval_check_test.cc
      forwarding_test.cc
      name_mangling_tests.cc)

    add_subdirectory(tutorials)
  endif(${BUILD_TESTING})

endif()

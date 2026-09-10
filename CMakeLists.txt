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
# The reflection flags are usage requirements of the protocol target (below)
# rather than directory-wide options, so that the installed package carries
# them to consumers; only the compiler probe needs them here. Clang's
# -stdlib=libc++ is directory-wide as well: every target in the tree,
# googletest included, must link the same standard library as the targets
# that use protocol.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_REQUIRED_FLAGS "-freflection")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-stdlib=libc++)
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
# One header list drives the build tree, the export and the install: a
# header missing here is missing from the installed package too.
target_sources(
  protocol
  INTERFACE FILE_SET
            HEADERS
            BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
            protocol.hh
            name_mangling.h)
# Consumers need the reflection flags of whichever compiler they build with,
# so the options are generator expressions evaluated at their configure time
# rather than the flags of the compiler that installed the package.
#
# Clang means the clang-p2996 fork (https://github.com/bloomberg/clang-p2996),
# the only Clang with P2996 support; upstream Clang cannot parse the sources.
# The fork enables reflection with -freflection-latest, expansion statements
# separately, and provides <meta> only in its bundled libc++.
# The flags are C++-only, so a consumer target that also has C sources must
# not see them on its C compilations.
target_compile_options(
  protocol
  INTERFACE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:-freflection>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:Clang>>:-freflection-latest;-fexpansion-statements;-stdlib=libc++>
)
target_link_options(
  protocol INTERFACE
  $<$<AND:$<LINK_LANGUAGE:CXX>,$<CXX_COMPILER_ID:Clang>>:-stdlib=libc++>)

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
    # Keep googletest's own install rules out of this project's install.
    set(INSTALL_GTEST
        OFF
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

  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
          DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/xyz_protocol")

  install(
    TARGETS protocol
    EXPORT xyz_protocol-targets
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  install(
    EXPORT xyz_protocol-targets
    NAMESPACE xyz_protocol::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/xyz_protocol")

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/xyz_protocol-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/xyz_protocol-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/xyz_protocol")

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/xyz_protocol-config-version.cmake"
    VERSION ${XYZ_PROTOCOL_VERSION}
    # SameMajorVersion would let every 0.x release satisfy a request for
    # 0.0; while the major version is 0 the minor version is the API level.
    COMPATIBILITY SameMinorVersion
    ARCH_INDEPENDENT)

  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/xyz_protocol-config.cmake"
                "${CMAKE_CURRENT_BINARY_DIR}/xyz_protocol-config-version.cmake"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/xyz_protocol")

endif()

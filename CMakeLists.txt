cmake_minimum_required(VERSION 3.22.4 FATAL_ERROR)
cmake_policy(SET CMP0127 NEW)
cmake_policy(SET CMP0135 NEW)

if(NOT DEFINED PROJECT_NAME)
    set(XYZ_PROTOCOL_IS_NOT_SUBPROJECT ON)
endif()

set(XYZ_PROTOCOL_VERSION 0.0.1)

project(protocol LANGUAGES CXX VERSION ${XYZ_PROTOCOL_VERSION})

include(CTest)
include(FetchContent)
include(GNUInstallDirs)
include(CMakeDependentOption)
include(CMakePackageConfigHelpers)

option(ENABLE_SANITIZERS "Enable Address Sanitizer and Undefined Behaviour Sanitizer if available" OFF)

# Include necessary submodules
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

# Custom CMake Includes
include(coverage)
include(xyz_add_library)
include(xyz_add_test)
include(xyz_add_object_library)

xyz_add_library(
    NAME protocol
    ALIAS xyz_value_types::protocol
)
target_sources(protocol
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/protocol.h>
)

xyz_add_object_library(
    NAME protocol_cc
    FILES protocol.cc
    LINK_LIBRARIES protocol
)

if (${XYZ_PROTOCOL_IS_NOT_SUBPROJECT})

    if (${BUILD_TESTING})
        FetchContent_Declare(
          googletest
          GIT_REPOSITORY https://github.com/google/googletest.git
          GIT_TAG        v1.15.2
        )
        # For Windows: Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        FetchContent_Declare(
            benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG        v1.9.4
        )
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(benchmark)

        enable_testing()

        xyz_add_test(
            NAME protocol_test
            LINK_LIBRARIES protocol
            FILES protocol_test.cc post_instantiation_example.h
        )

        if (ENABLE_CODE_COVERAGE)
            enable_code_coverage()
        endif()

    endif(${BUILD_TESTING})

endif()

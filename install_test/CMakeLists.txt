# A downstream project built against the installed package by
# `scripts/cmake.sh install`. Nothing here sets a C++ standard or a
# reflection flag: the imported target has to provide both.
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(protocol_install_test LANGUAGES CXX)

include(CTest)

find_package(xyz_protocol 0.0 REQUIRED)

add_executable(install_test main.cc)
target_link_libraries(install_test PRIVATE xyz_protocol::protocol)

add_test(NAME install_test COMMAND install_test)

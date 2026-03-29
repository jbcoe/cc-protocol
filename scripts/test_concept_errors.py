"""Tests for compiler error messages when protocol concepts are violated.

These tests verify that the C++ concepts and static asserts provide meaningful feedback.

These tests are run from CMake using CTest and require flags to be passed at run-time.
"""

import os
import re
import subprocess
import tempfile

import pytest


def pytest_addoption(parser):
    parser.addoption("--compiler", action="store", default="g++")
    parser.addoption("--flags", action="append", default=[])


@pytest.fixture
def compiler(request):
    return request.config.getoption("--compiler")


@pytest.fixture
def flags(request):
    return request.config.getoption("--flags")


def run_compiler(compiler, flags, source_code):
    with tempfile.TemporaryDirectory() as tmpdir:
        source_file = os.path.join(tmpdir, "test.cc")
        obj_file = os.path.join(tmpdir, "test.o")

        with open(source_file, "w") as f:
            f.write(source_code)

        cmd = [compiler] + flags + ["-c", source_file, "-o", obj_file]
        return subprocess.run(cmd, capture_output=True, text=True)


@pytest.fixture
def compile_check(compiler, flags):
    def check(source, expected_patterns):
        res = run_compiler(compiler, flags, source)
        assert res.returncode != 0, "Compilation should have failed"
        output = res.stderr + res.stdout
        for pattern in expected_patterns:
            assert re.search(pattern, output), (
                f"Expected pattern '{pattern}' not found in compiler output.\n"
                "--- Compiler Output ---\n"
                f"{output}\n"
                "-----------------------"
            )

    return check


def test_missing_method(compile_check):
    """Test that a class missing a required method fails to satisfy the protocol concept."""
    source = """
#ifndef XYZ_PROTOCOL_COMPILER_WRAPPER
    #include "generated/protocol_A.h"
#endif
    #include "interface_A.h"
    #include <utility>

    class BadALike_MissingMethod {
    public:
        int count() { return 42; }
    };

    void test() {
        xyz::protocol<xyz::A> a(std::in_place_type<BadALike_MissingMethod>);
    }
    """
    compile_check(
        source,
        [
            r"protocol_concept_A",
            r".name\(\)",
        ],
    )


def test_wrong_return_type(compile_check):
    """Test that a class with a method having a wrong return type fails to satisfy the protocol concept."""
    source = """
#ifndef XYZ_PROTOCOL_COMPILER_WRAPPER
    #include "generated/protocol_A.h"
#endif
    #include "interface_A.h"
    #include <utility>
    #include <string>

    class BadALike_WrongReturnType {
    public:
        std::string_view name() const { return "name"; }
        std::string count() { return "42"; }  // not convertible to int
    };

    void test() {
        xyz::protocol<xyz::A> a(std::in_place_type<BadALike_WrongReturnType>);
    }
    """
    compile_check(source, [r"protocol_concept_A", r"count\(\)"])


def test_primary_template_instantiation(compile_check):
    """Test that the primary protocol template cannot be instantiated."""
    source = """
    #include "protocol.h"
    struct NoSpecialization {};
    void test() { xyz::protocol<NoSpecialization> p; }
    """
    compile_check(
        source,
        [
            r"static assertion failed",
            r"The primary xyz::protocol template cannot be instantiated",
        ],
    )


def test_view_const_to_mutable_concrete(compile_check):
    """Test that a protocol_view for a mutable interface cannot be constructed from a const object."""
    source = """
#ifndef XYZ_PROTOCOL_COMPILER_WRAPPER
    #include "generated/protocol_A.h"
#endif
    #include "interface_A.h"

    struct MutALike {
        std::string_view name() const { return "name"; }
        int count() { return 1; } // Non-const
    };

    void test() {
        const MutALike a;
        xyz::protocol_view<xyz::A> view(a);
    }
    """
    # The error should indicate that protocol_concept_A is not satisfied because count() is not const
    compile_check(source, [r"protocol_concept_A", r"t.count\(\)"])


def test_view_const_to_mutable_protocol(compile_check):
    """Test that a protocol_view for a mutable interface cannot be constructed from a const protocol."""
    source = """
#ifndef XYZ_PROTOCOL_COMPILER_WRAPPER
    #include "generated/protocol_A.h"
#endif
    #include "interface_A.h"

    void test() {
        const xyz::protocol<xyz::A> a;
        xyz::protocol_view<xyz::A> view(a);
    }
    """
    compile_check(source, [r"protocol_concept_A", r"t.count\(\)"])


def test_view_const_alike_to_mutable(compile_check):
    """Test that a protocol_view for a mutable interface cannot be constructed from an object missing mutable methods."""
    source = """
#ifndef XYZ_PROTOCOL_COMPILER_WRAPPER
    #include "generated/protocol_A.h"
#endif
    #include "interface_A.h"

    struct ConstALike {
        std::string_view name() const { return "name"; }
    };

    void test() {
        ConstALike a;
        xyz::protocol_view<xyz::A> view(a);
    }
    """
    compile_check(source, [r"protocol_concept_A", r"t.count\(\)"])

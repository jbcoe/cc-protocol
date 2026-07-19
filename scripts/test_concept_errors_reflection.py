"""
Tests for compiler error messages from the C++26 reflection backend.

Unlike test_concept_errors.py, no per-interface generated header is needed:
the reflection backend synthesizes the same machinery from a plain struct at
compile time. Diagnostics also look different from the Python backend's: the
reflection backend's conformance concept is checked as one opaque boolean
(no per-member requires-clause), so failures are matched by the surrounding
constraint-failure text (reflection_protocol_concept, "no matching function
for call") rather than by the specific violating member call.

These tests are run from CMake using CTest and require flags to be passed at
run-time, and only when XYZ_PROTOCOL_ENABLE_REFLECTION_BACKEND is enabled.
"""

import os
import re
import subprocess
import tempfile
from typing import Any
from typing import Callable
from typing import List

import pytest


def pytest_addoption(parser: Any) -> None:
    """Add command-line options for the compiler and flags."""
    parser.addoption("--compiler", action="store", default="g++")
    parser.addoption("--flags", action="append", default=[])


@pytest.fixture
def compiler(request: Any) -> str:
    """Fixture that provides the compiler path."""
    return str(request.config.getoption("--compiler"))


@pytest.fixture
def flags(request: Any) -> List[str]:
    """Fixture that provides the compiler flags."""
    return list(request.config.getoption("--flags"))


def run_compiler(
    compiler: str, flags: List[str], source_code: str
) -> subprocess.CompletedProcess[str]:
    """Run the compiler on the given source code."""
    with tempfile.TemporaryDirectory() as tmpdir:
        source_file = os.path.join(tmpdir, "test.cc")
        obj_file = os.path.join(tmpdir, "test.o")

        with open(source_file, "w") as f:
            f.write(source_code)

        cmd = [compiler] + flags + ["-c", source_file, "-o", obj_file]
        return subprocess.run(cmd, capture_output=True, text=True)


@pytest.fixture
def compile_check(compiler: str, flags: List[str]) -> Callable[[str, List[str]], None]:
    """Fixture that provides a function to check compiler errors."""

    def check(source: str, expected_patterns: List[str]) -> None:
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


def test_missing_method(compile_check: Callable[[str, List[str]], None]) -> None:
    """Test that a class missing a required method fails to conform."""
    source = """
    #include "protocol.h"
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
        [r"reflection_protocol_concept", r"no matching function for call"],
    )


def test_wrong_return_type(compile_check: Callable[[str, List[str]], None]) -> None:
    """Test that a method whose return type isn't convertible fails."""
    source = """
    #include "protocol.h"
    #include "interface_A.h"
    #include <utility>
    #include <string>

    class BadALike_WrongReturnType {
    public:
        std::string_view name() const noexcept { return "name"; }
        std::string count() { return "42"; }  // not convertible to int
    };

    void test() {
        xyz::protocol<xyz::A> a(std::in_place_type<BadALike_WrongReturnType>);
    }
    """
    compile_check(
        source,
        [r"reflection_protocol_concept", r"no matching function for call"],
    )


def test_noexcept_violation(compile_check: Callable[[str, List[str]], None]) -> None:
    """
    Test that a method missing noexcept fails to conform.

    The vtable thunk and every forwarder for a noexcept interface member are
    themselves generated noexcept(true): accepting a throwing candidate would
    let an exception escape a noexcept function (terminating the program)
    instead of failing to compile.
    """
    source = """
    #include "protocol.h"
    #include "interface_A.h"
    #include <utility>

    class BadALike_NoExceptViolation {
    public:
        std::string_view name() const { return "name"; }  // Missing noexcept
        int count() { return 42; }
    };

    void test() {
        xyz::protocol<xyz::A> a(std::in_place_type<BadALike_NoExceptViolation>);
    }
    """
    compile_check(
        source,
        [r"reflection_protocol_concept", r"no matching function for call"],
    )


def test_reserved_member_name(compile_check: Callable[[str, List[str]], None]) -> None:
    """
    Test that an interface declaring 'swap' or 'valueless_after_move' fails.

    Those names are reserved for protocol's own public members and would be
    silently hidden by ordinary C++ name-hiding rules rather than reachable
    through the generated forwarders, so this is rejected with a
    static_assert naming the interface instead.
    """
    source = """
    #include "protocol.h"

    namespace xyz {
    struct ReservedNameInterface {
      void swap(int);
    };
    }

    void test() {
        xyz::protocol<xyz::ReservedNameInterface> p;
    }
    """
    compile_check(
        source,
        [
            r"static assertion failed",
            r"must not declare a member function named 'swap' or "
            r"'valueless_after_move'",
        ],
    )


def test_rvalue_qualified_member(
    compile_check: Callable[[str, List[str]], None],
) -> None:
    """Test that an interface declaring an rvalue-qualified (&&) member fails."""
    source = """
    #include "protocol.h"

    namespace xyz {
    struct RvalueMemberInterface {
      void consume() &&;
    };
    }

    void test() {
        xyz::protocol<xyz::RvalueMemberInterface> p;
    }
    """
    compile_check(
        source,
        [
            r"static assertion failed",
            r"must not declare an rvalue-qualified \(&&\) member function",
        ],
    )


def test_invalid_view_widening(compile_check: Callable[[str, List[str]], None]) -> None:
    """Verify that a protocol_view cannot be widened to one with more methods."""
    source = """
    #include "protocol.h"

    namespace xyz {
    struct Subset {
      int foo(int);
    };
    struct Wide {
      int foo(int);
      int bar(int);
    };
    }

    void test(xyz::protocol_view<xyz::Subset> view_subset) {
        xyz::protocol_view<xyz::Wide> view_wide(view_subset);
    }
    """
    compile_check(
        source,
        [
            r"static assertion failed",
            r"narrowing conversion requires every target interface member "
            r"to exist in the source interface with an identical signature",
        ],
    )


def test_invalid_protocol_widening(
    compile_check: Callable[[str, List[str]], None],
) -> None:
    """Verify that an owning protocol cannot be widened to one with more methods."""
    source = """
    #include "protocol.h"

    namespace xyz {
    struct Subset {
      int foo(int);
    };
    struct Wide {
      int foo(int);
      int bar(int);
    };
    }

    void test(xyz::protocol<xyz::Subset> p_subset) {
        xyz::protocol<xyz::Wide> p(std::move(p_subset));
    }
    """
    compile_check(source, [r"no matching function for call|no matching constructor"])


def test_narrowing_signature_mismatch(
    compile_check: Callable[[str, List[str]], None],
) -> None:
    """
    Verify that narrowing requires an identical signature, not just a name.

    A same-named member with a different parameter type is not the "same"
    interface member for narrowing purposes, even though it would be a
    perfectly good implementation candidate on its own.
    """
    source = """
    #include "protocol.h"

    namespace xyz {
    struct WideInterface {
      int foo(int);
    };
    struct NarrowMismatchInterface {
      int foo(double);
    };
    }

    void test(xyz::protocol_view<xyz::NarrowMismatchInterface> view_mismatch) {
        xyz::protocol_view<xyz::WideInterface> view(view_mismatch);
    }
    """
    compile_check(
        source,
        [
            r"static assertion failed",
            r"narrowing conversion requires every target interface member "
            r"to exist in the source interface with an identical signature",
        ],
    )

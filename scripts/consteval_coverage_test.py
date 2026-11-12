#!/usr/bin/env python3
"""
Tests for the Instrumenter in consteval_coverage.py.

Each test instruments a header snippet, checks that the probe points are
exactly the lines marked `// P`, and probes a translation unit that evaluates
the snippet: every probe point must fire, and the instrumented snippet must
compile. Requires a reflection-capable GCC (found via compiler_discovery).

Run with `uv run scripts/consteval_coverage_test.py`.
"""

import contextlib
import io
import os
import tempfile
import textwrap
import unittest

import consteval_coverage
from compiler_discovery import find_reflection_compilers

PROBE_MARKER = "// P"


class InstrumenterTest(unittest.TestCase):
    """Probe placement and firing for the constructs the scanner handles."""

    compiler: str

    @classmethod
    def setUpClass(cls) -> None:
        """Find the compiler the probes are compiled with."""
        _, compiler = find_reflection_compilers(dict(os.environ))
        if compiler is None:
            raise RuntimeError(
                "no reflection-capable compiler found "
                "(see scripts/compiler_discovery.py)"
            )
        cls.compiler = compiler

    def assert_probes(self, header: str, evaluation: str) -> None:
        """
        Instrument `header` and probe it with `evaluation`.

        The probe points must be the lines of `header` that end in `// P`. A
        translation unit that includes the instrumented header followed by
        `evaluation` must compile with no probe armed, and probing it must
        fire every probe point.
        """
        lines = textwrap.dedent(header).strip("\n").splitlines()
        expected = {
            line_number
            for line_number, line in enumerate(lines, start=1)
            if line.rstrip().endswith(PROBE_MARKER)
        }
        instrumenter = consteval_coverage.Instrumenter("snippet.hh", 0)
        instrumented = instrumenter.instrument(lines)
        found = {probe_point.line_number for probe_point in instrumenter.probe_points}
        self.assertEqual(found, expected, "\n".join(instrumented))

        with tempfile.TemporaryDirectory() as work_dir:
            with open(os.path.join(work_dir, "snippet.hh"), "w") as out:
                out.write("\n".join(instrumented) + "\n")
            with open(os.path.join(work_dir, "snippet.cc"), "w") as out:
                out.write(f'#include "snippet.hh"\n{evaluation}\n')
            configuration = consteval_coverage.ProbeConfiguration(
                compiler=self.compiler,
                work_dir=work_dir,
                include_directories=[],
                jobs=1,
            )
            # Unarmed, so a failure is the instrumenter's, even with no probes.
            consteval_coverage.compile_batch(
                configuration, "snippet.cc", 0, frozenset()
            )
            with contextlib.redirect_stdout(io.StringIO()):
                covered = consteval_coverage.probe_translation_unit(
                    configuration, "snippet.cc", {(0, line) for line in found}
                )
        fired = {line for _, line in covered}
        self.assertEqual(fired, found, "\n".join(instrumented))

    def test_consteval_function(self) -> None:
        """A consteval function's body, blocks and returns are probed."""
        self.assert_probes(
            """
            consteval int f(int x) {  // P
              if (x > 0) {  // P
                return 1;  // P
              }
              return 0;  // P
            }
            """,
            "static_assert(f(1) == 1); static_assert(f(0) == 0);",
        )

    def test_if_consteval_in_constexpr_function(self) -> None:
        """The first arm of `if consteval` is probed, and its `else` is not."""
        self.assert_probes(
            """
            constexpr int f() {
              if consteval {  // P
                return 2;  // P
              } else {
                return 1;
              }
            }
            """,
            "static_assert(f() == 2);",
        )

    def test_if_consteval_else_in_consteval_function(self) -> None:
        """The `else` of `if consteval` never runs during constant evaluation."""
        self.assert_probes(
            """
            consteval int f() {  // P
              if consteval {  // P
                return 2;  // P
              } else {
                return 1;
              }
            }
            """,
            "static_assert(f() == 2);",
        )

    def test_if_not_consteval_else(self) -> None:
        """The `else` of `if !consteval` runs during constant evaluation."""
        self.assert_probes(
            """
            constexpr int f() {
              if !consteval {
                return 1;
              } else {  // P
                return 2;  // P
              }
            }
            """,
            "static_assert(f() == 2);",
        )

    def test_if_not_consteval_in_consteval_function(self) -> None:
        """The block of `if !consteval` is not probed inside consteval code."""
        self.assert_probes(
            """
            consteval int f() {  // P
              if !consteval {
                return 1;
              }
              return 2;  // P
            }
            """,
            "static_assert(f() == 2);",
        )

    def test_if_not_keyword_consteval(self) -> None:
        """`if not consteval` is treated like `if !consteval`."""
        self.assert_probes(
            """
            consteval int f() {  // P
              if not consteval {
                return 1;
              }
              return 2;  // P
            }
            """,
            "static_assert(f() == 2);",
        )

    def test_if_consteval_inside_if_not_consteval(self) -> None:
        """An `if consteval` nested in `if !consteval` never runs."""
        self.assert_probes(
            """
            constexpr int f() {
              if !consteval {
                if consteval {
                  return 1;
                }
              }
              return 2;
            }
            """,
            "static_assert(f() == 2);",
        )

    def test_namespaced_forward_declaration(self) -> None:
        """A consteval forward declaration in a namespace opens no region."""
        self.assert_probes(
            """
            namespace xyz {
            consteval int g();
            constexpr int f() {
              return 1;
            }
            consteval int g() {  // P
              return f();  // P
            }
            }  // namespace xyz
            """,
            "static_assert(xyz::g() == 1);",
        )

    def test_lambda_in_return(self) -> None:
        """A `;` in a lambda body does not end the wrap of its `return`."""
        self.assert_probes(
            """
            consteval int f() {  // P
              return [] {  // P
                int x = 1;
                return x;
              }();
            }
            """,
            "static_assert(f() == 1);",
        )

    def test_braced_return(self) -> None:
        """A braced initialiser in a `return` is not a block."""
        self.assert_probes(
            """
            struct S {
              int a;
              int b;
            };
            consteval S f() {  // P
              return {1, 2};  // P
            }
            """,
            "static_assert(f().b == 2);",
        )

    def test_runtime_lambda(self) -> None:
        """The body of a `+[]` lambda runs at runtime and is not probed."""
        self.assert_probes(
            """
            using F = int (*)();
            consteval F f() {  // P
              return +[]() {  // P
                int y = 3;
                return y;
              };
            }
            """,
            "static_assert(f() != nullptr);",
        )


if __name__ == "__main__":
    unittest.main()

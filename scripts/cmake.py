#!/usr/bin/env python3
"""CMake helper script for building and testing the project."""

import argparse
import os
import platform
import re
import subprocess
from typing import Any

from compiler_discovery import find_reflection_compilers

# Root of a clang-p2996 toolchain (https://github.com/bloomberg/clang-p2996),
# the only Clang that can parse the reflection sources. --clang-tidy builds
# compile with its clang++ so that clang-tidy analyses exactly what the
# compiler accepted. Overridden by the XYZ_PROTOCOL_CLANG_P2996_DIRECTORY
# environment variable; CI provisions the toolchain at this default (see
# .github/workflows/clang-tidy.yml).
CLANG_P2996_DIRECTORY = os.environ.get(
    "XYZ_PROTOCOL_CLANG_P2996_DIRECTORY", "/opt/clang-p2996"
)

# Resolved from this script's own location rather than the current working
# directory, so default build directories land in the source root if this
# script is invoked from elsewhere.
SOURCE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The single literal directory .bazelignore (which supports no globs) needs
# to hide the CMake dependency sources, with their own incompatible
# BUILD.bazel files, from Bazel. Every build directory lives under it
# regardless of name.
BUILD_ROOT = os.path.join(SOURCE_ROOT, "build")


def _default_build_dir() -> str:
    """Build a default directory under BUILD_ROOT, distinct per OS and host."""
    host_identifier = re.sub(r"[^A-Za-z0-9.-]", "-", platform.node()) or "unknown-host"
    return os.path.join(BUILD_ROOT, f"{platform.system().lower()}-{host_identifier}")


DEFAULT_BUILD_DIR = _default_build_dir()


def _gcov_executable(cxx_path: str | None) -> str:
    """
    Return the gcov matching the compiler, e.g. gcov-16 for g++-16.

    Coverage notes written by one GCC version cannot be read by another
    version's gcov, so derive it from the C++ compiler path.
    """
    if cxx_path:
        directory, name = os.path.split(cxx_path)
        gcov_name = name.replace("g++", "gcov")
        if gcov_name != name:
            return os.path.join(directory, gcov_name) if directory else gcov_name
    return "gcov"


DEFAULT_CLANG_TIDY_BUILD_DIR = f"{DEFAULT_BUILD_DIR}.clang-tidy"


def main() -> None:
    """Execute the CMake build and test process based on command-line arguments."""
    parser = argparse.ArgumentParser(description="CMake helper script")
    parser.add_argument(
        "mode",
        nargs="?",
        default="test",
        choices=["build", "test", "b", "t"],
        help="Target mode: build (b), test (t) (default: test)",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--debug",
        action="store_const",
        dest="preset",
        const="Debug",
        help="Use Debug preset",
    )
    group.add_argument(
        "--release",
        action="store_const",
        dest="preset",
        const="Release",
        help="Use Release preset (default)",
    )
    parser.add_argument("--asan", action="store_true", help="Enable Address Sanitizer")
    parser.add_argument(
        "--ubsan", action="store_true", help="Enable Undefined Behaviour Sanitizer"
    )
    parser.add_argument("--tsan", action="store_true", help="Enable Thread Sanitizer")
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="Instrument with gcov, and report runtime coverage with gcovr "
        "after testing (uses the Debug preset; consteval code is invisible "
        "to gcov, measure it with scripts/consteval_coverage.py)",
    )
    parser.add_argument(
        "--clang-tidy",
        action="store_true",
        help="Run clang-tidy on every translation unit as it is compiled "
        "(requires a clang-p2996 toolchain, see "
        "cmake/modules/FindClangTidy.cmake; findings fail the build)",
    )
    parser.add_argument(
        "-B",
        "--build-dir",
        help="Build directory; relative paths are placed under build/",
    )
    parser.add_argument(
        "--clean", action="store_true", help="Fresh configuration and clean-first build"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging"
    )

    args, extra = parser.parse_known_args()

    # Determine preset: flag takes precedence, then default. Coverage numbers
    # from an optimised build are misleading (inlining and folding drop
    # lines), so --coverage builds Debug.
    if args.coverage:
        if args.preset == "Release":
            parser.error("--coverage requires the Debug preset")
        preset = "Debug"
    else:
        preset = args.preset if args.preset else "Release"

    # clang-tidy builds get their own directory by default, separate from
    # DEFAULT_BUILD_DIR: toggling CLANG_TIDY_ENABLE against that directory
    # would otherwise force a reconfigure every time --clang-tidy is turned
    # on or off.
    if not args.build_dir:
        args.build_dir = (
            DEFAULT_CLANG_TIDY_BUILD_DIR if args.clang_tidy else DEFAULT_BUILD_DIR
        )
    elif not os.path.isabs(args.build_dir):
        args.build_dir = os.path.join(BUILD_ROOT, args.build_dir)

    # Map abbreviations to full mode names
    mode_map = {
        "b": "build",
        "t": "test",
        "build": "build",
        "test": "test",
    }
    mode = mode_map[args.mode]

    def log(msg: Any) -> None:
        if args.verbose:
            print(msg)

    # Configure step
    configure_args = [
        "cmake",
        "--preset",
        preset,
        f"-DENABLE_ASAN={'ON' if args.asan else 'OFF'}",
        f"-DENABLE_UBSAN={'ON' if args.ubsan else 'OFF'}",
        f"-DENABLE_TSAN={'ON' if args.tsan else 'OFF'}",
        f"-DENABLE_COVERAGE={'ON' if args.coverage else 'OFF'}",
        f"-DCLANG_TIDY_ENABLE={'ON' if args.clang_tidy else 'OFF'}",
        "-B",
        args.build_dir,
    ]
    if args.clean:
        configure_args.append("--fresh")

    configure_args.extend(extra)

    # The implementation requires a C++26 reflection (P2996) compiler. CMake
    # only reads CXX/CC from the environment, not from -D cache variables, so
    # set them here rather than as configure_args.
    configure_env = os.environ.copy()
    if args.clang_tidy and "CXX" not in configure_env:
        clang_p2996_cxx = os.path.join(CLANG_P2996_DIRECTORY, "bin", "clang++")
        if os.path.exists(clang_p2996_cxx):
            configure_env["CXX"] = clang_p2996_cxx
            configure_env["CC"] = os.path.join(CLANG_P2996_DIRECTORY, "bin", "clang")
    cc_path, cxx_path = find_reflection_compilers(configure_env)
    if cxx_path is not None:
        configure_env["CXX"] = cxx_path
        if cc_path is not None:
            configure_env["CC"] = cc_path

    log(f"Running: {' '.join(configure_args)}")
    subprocess.check_call(configure_args, env=configure_env)

    # Build step (required for build and test)
    build_args = ["cmake", "--build", args.build_dir, "--config", preset]
    if args.clean:
        build_args.append("--clean-first")
    if args.clang_tidy:
        # Keep going after a failing translation unit so that one run reports
        # every clang-tidy finding rather than only the first file's.
        build_args.extend(["--", "-k", "0"])

    log(f"Running: {' '.join(build_args)}")
    subprocess.check_call(build_args)

    # Test step
    if mode == "test":
        test_args = [
            "ctest",
            "--output-on-failure",
            "--test-dir",
            args.build_dir,
            "-C",
            preset,
        ]

        log(f"Running: {' '.join(test_args)}")
        subprocess.check_call(test_args)

        if args.coverage:
            coverage_trace_path = os.path.join(args.build_dir, "coverage.info")
            gcovr_args = [
                "gcovr",
                "--root",
                SOURCE_ROOT,
                # Third-party sources fetched into the build directory.
                "--exclude",
                r".*/_deps/.*",
                "--gcov-executable",
                _gcov_executable(configure_env.get("CXX")),
                "--txt",
                "-",
                "--lcov",
                coverage_trace_path,
                args.build_dir,
            ]
            log(f"Running: {' '.join(gcovr_args)}")
            subprocess.check_call(gcovr_args)
            print(f"LCOV trace written to {coverage_trace_path}")


if __name__ == "__main__":
    main()

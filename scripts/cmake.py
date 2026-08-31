#!/usr/bin/env python3
"""CMake helper script for building and testing the project."""

import argparse
import os
import shutil
import subprocess
from typing import Any

# Directory of the GCC trunk snapshot published at
# https://jwakely.github.io/pkg-gcc-latest/, used as the default compiler
# when present (see the CXX handling in main below).
GCC_LATEST_BIN_DIRECTORY = "/opt/gcc-latest/bin"

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
DEFAULT_BUILD_DIR = os.path.join(SOURCE_ROOT, "build")
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
        "--clang-tidy",
        action="store_true",
        help="Run clang-tidy on every translation unit as it is compiled "
        "(requires a clang-p2996 toolchain, see "
        "cmake/modules/FindClangTidy.cmake; findings fail the build)",
    )
    parser.add_argument("-B", "--build-dir", help="Build directory")
    parser.add_argument(
        "--clean", action="store_true", help="Fresh configuration and clean-first build"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging"
    )

    args, extra = parser.parse_known_args()

    # Determine preset: flag takes precedence, then default.
    preset = args.preset if args.preset else "Release"

    # clang-tidy builds get their own directory by default, separate from
    # DEFAULT_BUILD_DIR: toggling CLANG_TIDY_ENABLE against that directory
    # would otherwise force a reconfigure every time --clang-tidy is turned
    # on or off.
    if not args.build_dir:
        args.build_dir = (
            DEFAULT_CLANG_TIDY_BUILD_DIR if args.clang_tidy else DEFAULT_BUILD_DIR
        )

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
        f"-DCLANG_TIDY_ENABLE={'ON' if args.clang_tidy else 'OFF'}",
        "-B",
        args.build_dir,
    ]
    if args.clean:
        configure_args.append("--fresh")

    configure_args.extend(extra)

    # The implementation requires a C++26 reflection (P2996) compiler. CMake
    # only reads CXX/CC from the environment, not from -D cache variables, so
    # set them here rather than as configure_args. An existing CXX in the
    # environment is left untouched so callers can override the compiler;
    # otherwise prefer the GCC trunk snapshot in /opt/gcc-latest (see
    # docker/Dockerfile) and fall back to Ubuntu's gcc-16 package if that
    # snapshot isn't installed. When neither is present CXX stays unset so
    # that CMake's default compiler reaches the reflection check in
    # CMakeLists.txt and reports which compiler lacks reflection, rather
    # than failing to find a compiler at all.
    configure_env = os.environ.copy()
    if "CXX" not in configure_env:
        clang_p2996_cxx = os.path.join(CLANG_P2996_DIRECTORY, "bin", "clang++")
        gcc_latest_cxx = os.path.join(GCC_LATEST_BIN_DIRECTORY, "g++")
        gcc_latest_cc = os.path.join(GCC_LATEST_BIN_DIRECTORY, "gcc")
        if args.clang_tidy and os.path.exists(clang_p2996_cxx):
            configure_env["CXX"] = clang_p2996_cxx
            configure_env["CC"] = os.path.join(CLANG_P2996_DIRECTORY, "bin", "clang")
        elif os.path.exists(gcc_latest_cxx):
            configure_env["CXX"] = gcc_latest_cxx
            configure_env["CC"] = gcc_latest_cc
        elif shutil.which("g++-16"):
            configure_env["CXX"] = "g++-16"
            configure_env["CC"] = "gcc-16"

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


if __name__ == "__main__":
    main()

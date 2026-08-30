#!/usr/bin/env python3
"""CMake helper script for building and testing the project."""

import argparse
import os
import subprocess
from typing import Any

# Directory of the GCC trunk snapshot published at
# https://jwakely.github.io/pkg-gcc-latest/, used as the default compiler
# when present (see the CXX handling in main below).
GCC_LATEST_BIN_DIRECTORY = "/opt/gcc-latest/bin"

# Resolved from this script's own location rather than the current working
# directory, so default build directories land in the source root if this
# script is invoked from elsewhere.
SOURCE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BUILD_DIR = os.path.join(SOURCE_ROOT, "build")


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

    if not args.build_dir:
        args.build_dir = DEFAULT_BUILD_DIR

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
    # snapshot isn't installed.
    configure_env = os.environ.copy()
    if "CXX" not in configure_env:
        gcc_latest_cxx = os.path.join(GCC_LATEST_BIN_DIRECTORY, "g++")
        gcc_latest_cc = os.path.join(GCC_LATEST_BIN_DIRECTORY, "gcc")
        if os.path.exists(gcc_latest_cxx):
            configure_env["CXX"] = gcc_latest_cxx
            configure_env["CC"] = gcc_latest_cc
        else:
            configure_env["CXX"] = "g++-16"
            configure_env["CC"] = "gcc-16"

    log(f"Running: {' '.join(configure_args)}")
    subprocess.check_call(configure_args, env=configure_env)

    # Build step (required for build and test)
    build_args = ["cmake", "--build", args.build_dir, "--config", preset]
    if args.clean:
        build_args.append("--clean-first")

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

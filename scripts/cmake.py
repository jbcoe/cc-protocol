#!/usr/bin/env python3
"""CMake helper script for building and testing the project."""

import argparse
import os
import platform
import re
import subprocess
from typing import Any

from compiler_discovery import find_reflection_compilers

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

    # Determine preset: flag takes precedence, then default.
    preset = args.preset if args.preset else "Release"

    if not args.build_dir:
        args.build_dir = DEFAULT_BUILD_DIR
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

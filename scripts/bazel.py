#!/usr/bin/env python3
"""Bazel helper script for building and testing the project."""

import argparse
import os
import shutil
import subprocess
from typing import Any
from typing import Tuple

# Directory of the GCC trunk snapshot published at
# https://jwakely.github.io/pkg-gcc-latest/, used as the default compiler
# when present (see the CXX handling in main below).
GCC_LATEST_BIN_DIRECTORY = "/opt/gcc-latest/bin"

# Resolved from this script's own location rather than the current working
# directory, so build operations run from the source root.
SOURCE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_system_compilers() -> Tuple[str, str]:
    """Find the paths for CC and CXX compilers supporting C++26 reflection."""
    gcc_latest_cxx_path = os.path.join(GCC_LATEST_BIN_DIRECTORY, "g++")
    gcc_latest_cc_path = os.path.join(GCC_LATEST_BIN_DIRECTORY, "gcc")

    if os.path.exists(gcc_latest_cxx_path) and os.path.exists(gcc_latest_cc_path):
        return gcc_latest_cc_path, gcc_latest_cxx_path

    system_gxx_path = shutil.which("g++-16")
    system_gcc_path = shutil.which("gcc-16")
    if system_gxx_path and system_gcc_path:
        return system_gcc_path, system_gxx_path

    # Fallback to standard gcc/g++ if version 16 is not explicitly named
    default_gxx_path = shutil.which("g++") or "g++"
    default_gcc_path = shutil.which("gcc") or "gcc"
    return default_gcc_path, default_gxx_path


def create_compiler_wrappers(real_cc_path: str, real_cxx_path: str) -> Tuple[str, str]:
    """Create wrapper scripts to filter Clang-only flags for GCC."""
    wrappers_directory_path = os.path.join(SOURCE_ROOT, ".bazel-wrappers")
    os.makedirs(wrappers_directory_path, exist_ok=True)

    compiler_wrapper_script_path = os.path.join(
        SOURCE_ROOT, "scripts", "bazel_compiler_wrapper.py"
    )

    gcc_wrapper_path = os.path.join(wrappers_directory_path, "gcc")
    gxx_wrapper_path = os.path.join(wrappers_directory_path, "g++")

    wrapper_template = """#!/bin/bash
exec python3 "{compiler_wrapper_script_path}" "{real_binary_path}" "$@"
"""

    with open(gcc_wrapper_path, "w", encoding="utf-8") as gcc_file:
        gcc_file.write(
            wrapper_template.format(
                compiler_wrapper_script_path=compiler_wrapper_script_path,
                real_binary_path=real_cc_path,
            )
        )
    os.chmod(gcc_wrapper_path, 0o755)

    with open(gxx_wrapper_path, "w", encoding="utf-8") as gxx_file:
        gxx_file.write(
            wrapper_template.format(
                compiler_wrapper_script_path=compiler_wrapper_script_path,
                real_binary_path=real_cxx_path,
            )
        )
    os.chmod(gxx_wrapper_path, 0o755)

    return gcc_wrapper_path, gxx_wrapper_path


def main() -> None:
    """Execute the Bazel build and test process based on command-line arguments."""
    argument_parser = argparse.ArgumentParser(description="Bazel helper script")
    argument_parser.add_argument(
        "mode",
        nargs="?",
        default="test",
        choices=["build", "test", "b", "t"],
        help="Target mode: build (b), test (t) (default: test)",
    )
    argument_parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean Bazel build artifacts and expunge cache",
    )
    argument_parser.add_argument(
        "-v", "--verbose", action="store_true", help="Enable verbose logging"
    )

    parsed_arguments, extra_arguments = argument_parser.parse_known_args()

    mode_map = {
        "b": "build",
        "t": "test",
        "build": "build",
        "test": "test",
    }
    target_mode = mode_map[parsed_arguments.mode]

    def log_message(message_content: Any) -> None:
        if parsed_arguments.verbose:
            print(message_content)

    bazel_environment = os.environ.copy()

    if "CC" in bazel_environment and "CXX" in bazel_environment:
        cc_target_path = bazel_environment["CC"]
        cxx_target_path = bazel_environment["CXX"]
    else:
        cc_target_path, cxx_target_path = find_system_compilers()

    gcc_wrapper_path, gxx_wrapper_path = create_compiler_wrappers(
        cc_target_path, cxx_target_path
    )

    bazel_environment["CC"] = gcc_wrapper_path
    bazel_environment["CXX"] = gxx_wrapper_path
    bazel_environment["BAZEL_LINKLIBS"] = "-lstdc++:-lm"

    if parsed_arguments.clean:
        clean_command = ["bazel", "clean", "--expunge"]
        log_message(f"Running: {' '.join(clean_command)}")
        subprocess.check_call(clean_command, cwd=SOURCE_ROOT, env=bazel_environment)

    bazel_command = ["bazel", target_mode]
    bazel_command.append(f"--action_env=CC={gcc_wrapper_path}")
    bazel_command.append(f"--repo_env=CC={gcc_wrapper_path}")
    bazel_command.append(f"--action_env=CXX={gxx_wrapper_path}")
    bazel_command.append(f"--repo_env=CXX={gxx_wrapper_path}")
    bazel_command.append("--action_env=BAZEL_LINKLIBS")
    bazel_command.append("--repo_env=BAZEL_LINKLIBS")
    bazel_command.append("--action_env=PATH")
    bazel_command.append("--repo_env=PATH")

    bazel_command.append("//...")
    bazel_command.extend(extra_arguments)

    log_message(f"Running: {' '.join(bazel_command)}")
    subprocess.check_call(bazel_command, cwd=SOURCE_ROOT, env=bazel_environment)


if __name__ == "__main__":
    main()

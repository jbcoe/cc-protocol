#!/usr/bin/env python3
"""Bazel helper script for building and testing the project."""

import argparse
import os
import shutil
import subprocess
from typing import Any
from typing import Optional
from typing import Tuple

# Directory of the GCC trunk snapshot published at
# https://jwakely.github.io/pkg-gcc-latest/, used as the default compiler
# when present (see the CXX handling in main below).
GCC_LATEST_BIN_DIRECTORY = "/opt/gcc-latest/bin"

# Resolved from this script's own location rather than the current working
# directory, so build operations run from the source root.
SOURCE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_libstdcxx_directory(cxx_compiler_path: str) -> Optional[str]:
    """Find the directory containing libstdc++.so for GCC trunk or custom toolchains."""
    try:
        compiler_output = subprocess.check_output(
            [cxx_compiler_path, "-print-file-name=libstdc++.so"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        if compiler_output and os.path.exists(compiler_output):
            canonical_path = os.path.realpath(compiler_output)
            return os.path.dirname(canonical_path)
    except Exception:
        pass

    gcc_latest_root = os.path.dirname(GCC_LATEST_BIN_DIRECTORY)
    if cxx_compiler_path.startswith(gcc_latest_root):
        for candidate_subpath in ("lib64", "lib"):
            candidate_directory = os.path.join(gcc_latest_root, candidate_subpath)
            if os.path.isdir(candidate_directory):
                return candidate_directory

    return None


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
        cc_environment_path = bazel_environment["CC"]
        cxx_environment_path = bazel_environment["CXX"]
        cc_target_path = shutil.which(cc_environment_path) or cc_environment_path
        cxx_target_path = shutil.which(cxx_environment_path) or cxx_environment_path
    else:
        cc_target_path, cxx_target_path = find_system_compilers()

    bazel_environment["CC"] = cc_target_path
    bazel_environment["CXX"] = cxx_target_path

    if parsed_arguments.clean:
        clean_command = ["bazel", "clean", "--expunge"]
        log_message(f"Running: {' '.join(clean_command)}")
        subprocess.check_call(clean_command, cwd=SOURCE_ROOT, env=bazel_environment)

    bazel_command = ["bazel", target_mode]
    bazel_command.append(f"--action_env=CC={cc_target_path}")
    bazel_command.append(f"--repo_env=CC={cc_target_path}")
    bazel_command.append(f"--action_env=CXX={cxx_target_path}")
    bazel_command.append(f"--repo_env=CXX={cxx_target_path}")
    bazel_command.append("--action_env=PATH")
    bazel_command.append("--repo_env=PATH")

    libstdcxx_directory = find_libstdcxx_directory(cxx_target_path)
    if libstdcxx_directory:
        bazel_command.append(f"--linkopt=-Wl,-rpath,{libstdcxx_directory}")
        existing_ld_path = bazel_environment.get("LD_LIBRARY_PATH", "")
        full_ld_path = (
            f"{libstdcxx_directory}:{existing_ld_path}"
            if existing_ld_path
            else libstdcxx_directory
        )
        bazel_command.append(f"--test_env=LD_LIBRARY_PATH={full_ld_path}")
        bazel_environment["LD_LIBRARY_PATH"] = full_ld_path

    bazel_command.append("//...")
    bazel_command.extend(extra_arguments)

    log_message(f"Running: {' '.join(bazel_command)}")
    subprocess.check_call(bazel_command, cwd=SOURCE_ROOT, env=bazel_environment)


if __name__ == "__main__":
    main()

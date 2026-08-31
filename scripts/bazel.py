#!/usr/bin/env python3
"""Bazel helper script for building and testing the project."""

import argparse
import os
import shutil
import subprocess
from typing import Any

from compiler_discovery import find_libstdcxx_directory
from compiler_discovery import find_reflection_compilers

# Resolved from this script's own location rather than the current working
# directory, so build operations run from the source root.
SOURCE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


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

    parsed_arguments = argument_parser.parse_args()

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

    cc_path, cxx_path = find_reflection_compilers(bazel_environment)

    if parsed_arguments.clean:
        clean_command = ["bazel", "clean", "--expunge"]
        log_message(f"Running: {' '.join(clean_command)}")
        subprocess.check_call(clean_command, cwd=SOURCE_ROOT, env=bazel_environment)

    bazel_command = ["bazel", target_mode]
    bazel_command.append("--action_env=PATH")
    bazel_command.append("--repo_env=PATH")

    # Bazel's repository rules need concrete, absolute compiler paths rather
    # than bare names resolved via PATH. If none was found, leave CC/CXX
    # unset so Bazel falls through to its own default toolchain and reports
    # that it lacks reflection support, matching scripts/cmake.py's policy.
    if cxx_path is not None:
        cxx_target_path = shutil.which(cxx_path) or cxx_path
        bazel_environment["CXX"] = cxx_target_path
        bazel_command.append(f"--action_env=CXX={cxx_target_path}")
        bazel_command.append(f"--repo_env=CXX={cxx_target_path}")

        # Bazel's autoconfigured C++ toolchain selects the compiler from CC and
        # never reads CXX. A caller that exports CXX alone (scripts/cmake.py's
        # convention) leaves cc_path unset, so derive the sibling gcc next to
        # g++ rather than letting Bazel fall back to the default /usr/bin/gcc.
        if cc_path is None:
            cxx_directory, cxx_name = os.path.split(cxx_target_path)
            sibling_cc_path = os.path.join(
                cxx_directory, cxx_name.replace("g++", "gcc")
            )
            if os.path.exists(sibling_cc_path):
                cc_path = sibling_cc_path

        if cc_path is not None:
            cc_target_path = shutil.which(cc_path) or cc_path
            bazel_environment["CC"] = cc_target_path
            bazel_command.append(f"--action_env=CC={cc_target_path}")
            bazel_command.append(f"--repo_env=CC={cc_target_path}")

        # The compiler binary is not itself an action input, so a constant path
        # whose contents change (the /opt/gcc-latest trunk snapshot is replaced
        # weekly in place) would otherwise replay cached objects built by the
        # previous compiler. Stamping the version into every action and repo
        # rule key ties the cache to the compiler's identity, not just its path.
        compiler_version = subprocess.check_output(
            [cxx_target_path, "--version"], text=True
        ).splitlines()[0]
        bazel_command.append(
            f"--action_env=XYZ_PROTOCOL_COMPILER_VERSION={compiler_version}"
        )
        bazel_command.append(
            f"--repo_env=XYZ_PROTOCOL_COMPILER_VERSION={compiler_version}"
        )

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

    log_message(f"Running: {' '.join(bazel_command)}")
    subprocess.check_call(bazel_command, cwd=SOURCE_ROOT, env=bazel_environment)


if __name__ == "__main__":
    main()

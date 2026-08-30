#!/usr/bin/env python3
"""
Compiler wrapper to filter unsupported Clang-specific flags.

Reference implementations of compiler wrappers:
- Spack compiler wrapper (flag/rpath injection and toolchain abstraction):
  https://github.com/spack/spack/blob/v0.22.0/lib/spack/env/cc
- Bazel rules_cc macOS wrapper script:
  https://github.com/bazelbuild/rules_cc/blob/main/cc/private/toolchain/osx_cc_wrapper.sh.tpl
- Bazel apple_support compiler wrapper:
  https://github.com/bazelbuild/apple_support/blob/main/crosstool/wrapped_clang.cc

Filters unsupported Clang-specific flags (e.g. -ffile-compilation-dir=*,
-fobjc-*, -fembed-bitcode) from command-line arguments and response files
(@path.params) before invoking GCC.
"""

import os
import subprocess
import sys
from typing import List
from typing import Optional
from typing import Set
from typing import Tuple

CLANG_ONLY_FLAG_PREFIXES: Tuple[str, ...] = (
    "-ffile-compilation-dir=",
    "-fobjc-",
    "-fembed-bitcode",
    "-stdlib=",
)

CLANG_ONLY_EXACT_FLAGS: Set[str] = {
    "-fobjc-link-runtime",
    "-fobjc-arc",
    "-fembed-bitcode",
    "-fembed-bitcode-marker",
    "-stdlib=libc++",
}


def transform_argument(argument: str) -> Optional[str]:
    """
    Transform or filter an argument for GCC.

    Returns the transformed argument, or None if the argument should be dropped.
    """
    if argument in CLANG_ONLY_EXACT_FLAGS:
        return None
    for prefix in CLANG_ONLY_FLAG_PREFIXES:
        if argument.startswith(prefix):
            return None
    # Map Apple Clang's C++ standard library to GNU libstdc++
    if argument == "-lc++":
        return "-lstdc++"
    return argument


def process_response_file(response_file_path: str) -> str:
    """Read a Bazel response file (@file.params) and filter unsupported flags."""
    new_response_file_path = response_file_path + ".gcc_filtered"
    with open(
        response_file_path, "r", encoding="utf-8", errors="replace"
    ) as input_file:
        lines = input_file.readlines()

    filtered_lines: List[str] = []
    seen_libraries: Set[str] = set()
    for line in lines:
        stripped_argument = line.strip()
        transformed = transform_argument(stripped_argument)
        if transformed is not None:
            # Deduplicate libraries to avoid Apple ld duplicate library warnings
            if transformed.startswith("-l"):
                if transformed in seen_libraries:
                    continue
                seen_libraries.add(transformed)
            filtered_lines.append(transformed + "\n")

    with open(new_response_file_path, "w", encoding="utf-8") as output_file:
        output_file.writelines(filtered_lines)

    return new_response_file_path


def main() -> None:
    """Execute the underlying compiler with filtered arguments and response files."""
    if len(sys.argv) < 2:
        sys.stderr.write(
            "Usage: bazel_compiler_wrapper.py <real_compiler_path> [args...]\n"
        )
        sys.exit(1)

    real_compiler_path = sys.argv[1]
    raw_arguments = sys.argv[2:]

    # Resolve symlinks so GCC can reliably find sibling passes (like cc1plus in libexec)
    canonical_compiler_path = os.path.realpath(real_compiler_path)

    # Ensure compiler binary directory and realpath directory are in PATH
    compiler_dir = os.path.dirname(real_compiler_path)
    canonical_dir = os.path.dirname(canonical_compiler_path)
    current_path = os.environ.get("PATH", "")
    path_components = [compiler_dir, canonical_dir]
    for element in current_path.split(os.pathsep):
        if element and element not in path_components:
            path_components.append(element)
    os.environ["PATH"] = os.pathsep.join(path_components)

    target_command: List[str] = [canonical_compiler_path]

    seen_libraries: Set[str] = set()
    for argument in raw_arguments:
        if argument.startswith("@") and os.path.isfile(argument[1:]):
            response_file_path = argument[1:]
            new_response_path = process_response_file(response_file_path)
            target_command.append("@" + new_response_path)
        else:
            transformed = transform_argument(argument)
            if transformed is not None:
                if transformed.startswith("-l"):
                    if transformed in seen_libraries:
                        continue
                    seen_libraries.add(transformed)
                target_command.append(transformed)

    result = subprocess.call(target_command)
    sys.exit(result)


if __name__ == "__main__":
    main()

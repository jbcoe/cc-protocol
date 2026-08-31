"""Selects a C++26 reflection compiler, shared by build scripts."""

import os
import shutil
import subprocess
from typing import Mapping
from typing import Optional
from typing import Tuple

_GCC_LATEST_BIN_DIRECTORY = "/opt/gcc-latest/bin"


def find_reflection_compilers(
    environ: Mapping[str, str],
) -> Tuple[Optional[str], Optional[str]]:
    """Resolve (CC, CXX) for a C++26 reflection-capable GCC."""
    if "CXX" in environ:
        return environ.get("CC"), environ["CXX"]

    gcc_latest_cxx_path = os.path.join(_GCC_LATEST_BIN_DIRECTORY, "g++")
    gcc_latest_cc_path = os.path.join(_GCC_LATEST_BIN_DIRECTORY, "gcc")
    if os.path.exists(gcc_latest_cxx_path) and os.path.exists(gcc_latest_cc_path):
        return gcc_latest_cc_path, gcc_latest_cxx_path

    system_gxx_path = shutil.which("g++-16")
    system_gcc_path = shutil.which("gcc-16")
    if system_gxx_path and system_gcc_path:
        return system_gcc_path, system_gxx_path

    return None, None


def find_libstdcxx_directory(cxx_compiler_path: str) -> Optional[str]:
    """
    Resolve the libstdc++.so directory for cxx_compiler_path.

    Mirrors the rpath lookup in CMakeLists.txt: a non-distro GCC keeps its
    libstdc++ in a directory ld.so does not search by default, so callers
    linking against it need this directory to run the result without
    LD_LIBRARY_PATH set.
    """
    try:
        compiler_output = subprocess.check_output(
            [cxx_compiler_path, "-print-file-name=libstdc++.so"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        if compiler_output and os.path.exists(compiler_output):
            canonical_path = os.path.realpath(compiler_output)
            return os.path.dirname(canonical_path)
    except (OSError, subprocess.CalledProcessError):
        pass

    gcc_latest_root = os.path.dirname(_GCC_LATEST_BIN_DIRECTORY)
    if cxx_compiler_path.startswith(gcc_latest_root):
        for candidate_subpath in ("lib64", "lib"):
            candidate_directory = os.path.join(gcc_latest_root, candidate_subpath)
            if os.path.isdir(candidate_directory):
                return candidate_directory

    return None

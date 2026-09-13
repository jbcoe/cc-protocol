"""Selects a C++26 reflection compiler, shared by build scripts."""

import os
import shutil
import subprocess
import sys
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

    if sys.platform == "darwin":
        # gcc@16 is keg-only once it is no longer Homebrew's default `gcc`,
        # so it drops off PATH and the shutil.which check above stops
        # finding it; ask Homebrew directly instead.
        try:
            gcc16_prefix = subprocess.check_output(
                ["brew", "--prefix", "gcc@16"], text=True, stderr=subprocess.DEVNULL
            ).strip()
        except (OSError, subprocess.CalledProcessError):
            gcc16_prefix = None
        if gcc16_prefix:
            homebrew_cxx_path = os.path.join(gcc16_prefix, "bin", "g++-16")
            homebrew_cc_path = os.path.join(gcc16_prefix, "bin", "gcc-16")
            if os.path.exists(homebrew_cxx_path) and os.path.exists(homebrew_cc_path):
                return homebrew_cc_path, homebrew_cxx_path

    return None, None


def find_runtime_library_directory(cxx_compiler_path: str) -> Optional[str]:
    """
    Resolve the C++ runtime library directory for cxx_compiler_path.

    libc++ for Clang (the clang-p2996 fork keeps it in its own build tree),
    libstdc++ otherwise. Linux only: on macOS, Homebrew's GCC already embeds
    its own rpath, and ld.so's search-path gap this exists to work around
    (see find_libstdcxx_directory) has no macOS/dyld equivalent.
    """
    if sys.platform == "darwin":
        return None
    if "clang" in os.path.basename(cxx_compiler_path):
        return _find_library_directory(cxx_compiler_path, "libc++.so")
    return find_libstdcxx_directory(cxx_compiler_path)


def find_libstdcxx_directory(cxx_compiler_path: str) -> Optional[str]:
    """
    Resolve the libstdc++.so directory for cxx_compiler_path.

    Mirrors the rpath lookup in CMakeLists.txt: a non-distro GCC keeps its
    libstdc++ in a directory ld.so does not search by default, so callers
    linking against it need this directory to run the result without
    LD_LIBRARY_PATH set. Linux only, see find_runtime_library_directory.
    """
    if sys.platform == "darwin":
        return None
    library_directory = _find_library_directory(cxx_compiler_path, "libstdc++.so")
    if library_directory is not None:
        return library_directory

    gcc_latest_root = os.path.dirname(_GCC_LATEST_BIN_DIRECTORY)
    if cxx_compiler_path.startswith(gcc_latest_root):
        for candidate_subpath in ("lib64", "lib"):
            candidate_directory = os.path.join(gcc_latest_root, candidate_subpath)
            if os.path.isdir(candidate_directory):
                return candidate_directory

    return None


def _find_library_directory(cxx_compiler_path: str, library_name: str) -> Optional[str]:
    """Ask the compiler where it keeps library_name; None if it does not know."""
    try:
        compiler_output = subprocess.check_output(
            [cxx_compiler_path, f"-print-file-name={library_name}"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        if compiler_output and os.path.exists(compiler_output):
            canonical_path = os.path.realpath(compiler_output)
            return os.path.dirname(canonical_path)
    except (OSError, subprocess.CalledProcessError):
        pass

    return None

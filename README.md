# `protocol`: Structural Subtyping for C++

[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/C%2B%2B-20%2B-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

We propose the addition of two class templates, `protocol<T, A>` and
`protocol_view<T>`, to the C++ Standard Library. Both classes support
structural subtyping: `protocol` is owning (allocator-aware), and
`protocol_view` is non-owning.

See [DRAFT.md](DRAFT.md) for complete details on the proposal design and library specifications.

## Implementation Architecture & Backends

This repository contains both the ISO C++ proposal paper and a reference implementation supporting two code-generation backends:

1. Python Build-Step Backend (Default): Uses `libclang` (`py_cppmodel`) and Jinja2 templates ([scripts/protocol.j2](scripts/protocol.j2)) to parse C++ interface headers and generate static vtable specializations at build time. Supported on all target C++20 compilers (GCC 11-14, Clang 17-21, MSVC, Apple Clang).
2. C++26 Reflection Backend (Opt-In): Uses C++26 reflection ([P2996R13]) via [protocol_reflection.h](protocol_reflection.h) to synthesize vtables, forwarding wrappers, and concepts entirely inside the compiler at compile time without any external Python generation step. Requires GCC 16+ with `-freflection`.

Both backends share the same public API in [protocol.h](protocol.h), support narrowing conversions, and pass the identical test suite. Full removal of the Python code-generation step remains future work as compiler reflection implementations mature.

For deep architectural details, see [implementation-notes.md](implementation-notes.md).

## Quick Start & Building

Ensure you have [CMake](https://cmake.org/), a modern C++ compiler, and [uv](https://docs.astral.sh/uv/) installed.

```bash
# Build and run tests with default Python backend
./scripts/cmake.sh

# Build and run tests with the C++26 Reflection backend (requires GCC 16+)
CXX=g++-16 CC=gcc-16 ./scripts/cmake.sh --release \
    -DXYZ_PROTOCOL_ENABLE_REFLECTION_BACKEND=ON -B build-reflection
```

For complete build options, benchmarking, and development practices, see [CONTRIBUTING.md](CONTRIBUTING.md).

## Standardization

The paper [P4148R2](https://wg21.link/P4148R2.pdf) (derived from
[DRAFT.md](DRAFT.md)) was presented to the C++ Standard Library Incubator
working group in Brno on June 11th 2026. The authors have been encouraged to
continue work.

## GitHub Codespaces

Press `.` or visit [https://github.dev/jbcoe/cc-protocol](https://github.dev/jbcoe/cc-protocol) to open the project in an instant, cloud-based development environment. We provide a [devcontainer](.devcontainer/devcontainer.json) pre-configured with all required dependencies, including GCC 16 for C++26 reflection experiments.

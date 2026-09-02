# Gemini Project Context: protocol

This file provides project-specific mandates and conventions that override
general defaults for this repository.

## Engineering Standards

- **C++ Specification:** Target C++20/26. Prioritize value semantics, type
  erasure, and allocator-aware designs consistent with P3019
  (`std::polymorphic`).
- **Naming Conventions:** NEVER use abbreviations in public or internal variable
  names. Use descriptive names like `XYZ_PROTOCOL_LIBSTDCXX_DIRECTORY` instead
  of `XYZ_PROTO_LIBSTDCXX_DIR`.
- **WG21 Style:** `DRAFT.md` must adhere to ISO C++ standardization proposal
  norms <https://www.open-std.org/jtc1/sc22/wg21/docs/papers>
- **Paper Format:** We use pure Markdown, no YAML frontmatter, and no HTML blocks.

## Workflow Mandates

- **Tooling:** Always use `uv` for Python dependency management (`uv run ...`).
- **Build & Test:** Prefer `scripts/bazel.sh` for building and testing; it
  is markedly faster than the CMake build, both from scratch and after
  editing `protocol.hh`. Use `scripts/cmake.sh` only for what Bazel does not
  provide: coverage and clang-tidy. Both entrypoints discover a
  reflection-capable compiler and pass it to the build system; a bare
  `bazel`/`cmake` invocation uses stock GCC and fails on `-freflection`.
  The `scripts/bazel.sh` entrypoint supports `build`/`test`, `--clean`,
  `--asan`, `--ubsan`, and `--tsan`; `scripts/cmake.sh` supports `--debug`,
  `--release`, `--asan`, `--ubsan`, `--tsan`, `--coverage`, and
  `--clang-tidy`.
- **Compiler:** The implementation requires GCC with C++26 reflection (the GCC
  trunk snapshot in `/opt/gcc-latest`, or `gcc-16`); CI, including the
  sanitizer jobs, runs on GCC trunk.
- **Verification:** All changes must be verified by building and testing the
  implementation, with `scripts/bazel.sh` by default. Changes to CMake files
  or to `scripts/cmake.py` must also be verified with `scripts/cmake.sh`.
- **Sanitizer Verification:** When modifying memory-sensitive or concurrent
  code, verify changes locally using at least one sanitizer (e.g.,
  `./scripts/bazel.sh --asan` or `--tsan`; `scripts/cmake.sh` takes the same
  flags). CI runs the sanitizers under both build systems. Note that ASAN and
  TSAN are mutually exclusive.
- **Post-Change Checks:** Tests and pre-commit checks MUST be run after any
  modifications to the codebase.

## Git Usage

- **Source Control:** This repository uses git.
- **History Integrity:** NEVER use git commands that affect the git history.
- **Commit & Branching:** Never commit changes, create, or delete branches.
- **Human Intervention:** If git commands must be run, you MUST ask for human
  intervention.

## Critical Paths

- Implementation: `protocol.hh`
- Proposal Draft: `DRAFT.md`
- Build Entrypoints: `scripts/bazel.sh` (Bazel, preferred), `scripts/cmake.sh`
  (CMake: coverage, clang-tidy)

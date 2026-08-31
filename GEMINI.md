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
- **Build & Test:** Use `scripts/cmake.sh` for all build and test operations.
  The `scripts/cmake.sh` entrypoint supports `--debug`, `--release`,
  `--asan`, `--ubsan`, `--tsan`, and `--clang-tidy`.
- **Compiler:** The implementation requires GCC with C++26 reflection (the GCC
  trunk snapshot in `/opt/gcc-latest`, or `gcc-16`); CI, including the
  sanitizer jobs, runs on GCC trunk.
- **Verification:** All changes must be verified using the `scripts/cmake.sh`
  script to build and test the implementation.
- **Sanitizer Verification:** When modifying memory-sensitive or concurrent
  code, verify changes locally using at least one sanitizer (e.g.,
  `./scripts/cmake.sh --asan` or `--tsan`). Note that ASAN and TSAN are
  mutually exclusive.
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
- Build Entrypoint: `scripts/cmake.sh`

# Gemini Project Context: protocol

This file provides project-specific mandates and conventions that override
general defaults for this repository.

## Engineering Standards

- **C++ Specification:** Target C++20/26. Prioritize value semantics, type
  erasure, and allocator-aware designs consistent with P3019
  (`std::polymorphic`).
- **Naming Conventions:** NEVER use abbreviations in public or internal variable
  names. Use descriptive names like `XYZ_GENERATE_MANUAL_VTABLE` instead of
  `XYZ_GEN_MAN_VT`.
- **Stability:** Ensure that generated symbols (e.g., vtable entry names) remain
  deterministic and stable between runs by using MD5 hashing of function
  signatures.
- **WG21 Style:** `DRAFT.md` must adhere to ISO C++ standardization proposal
  norms <https://www.open-std.org/jtc1/sc22/wg21/docs/papers>
- **Paper Format:** We use pure Markdown, no YAML frontmatter, and no HTML blocks.

## Workflow Mandates

- **Tooling:** Always use `uv` for Python dependency management (`uv run ...`).
- **Build & Test:** Use `scripts/cmake.sh` for all build and test operations.
  The `scripts/cmake.sh` entrypoint supports `--debug`, `--release`,
  `--manual-vtable`, `--asan`, `--ubsan`, `--tsan`, and `--msan`.
- **Compiler Preferences:** Prefer Clang 19+ for sanitizer-based verification
  and CI, as it provides superior support for MSAN and TSAN compared to
  older GCC versions.
- **Verification:** All changes must be verified against both the default
  (virtual dispatch) and manual vtable configurations. The `scripts/cmake.sh`
  script must be run twice: once without any flags, and a second time with
  the `--manual-vtable` flag to build and test the alternative implementation.
- **Sanitizer Verification:** When modifying memory-sensitive or concurrent
  code, verify changes locally using at least one sanitizer (e.g.,
  `./scripts/cmake.sh --asan` or `--tsan`). Note that ASAN, TSAN, and MSAN
  are mutually exclusive.
- **Post-Change Checks:** Tests and pre-commit checks MUST be run after any
  modifications to the codebase.

## Git Usage

- **Source Control:** This repository uses git.
- **History Integrity:** NEVER use git commands that affect the git history.
- **Commit & Branching:** Never commit changes, create, or delete branches.
- **Human Intervention:** If git commands must be run, you MUST ask for human
  intervention.

## Critical Paths

- Generation Script: `scripts/generate_protocol.py`
- Proposal Draft: `DRAFT.md`
- Build Entrypoint: `scripts/cmake.sh`

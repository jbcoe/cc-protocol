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
  norms: pure Markdown, no YAML frontmatter, and no HTML blocks.

## Workflow Mandates

- **Tooling:** Always use `uv` for Python dependency management (`uv run ...`).
- **Build & Test:** Use `scripts/cmake.sh` for all build and test operations.
- **Verification:** All changes must be verified against both the default (virtual dispatch) and manual vtable configurations. The `scripts/cmake.sh` script must be run twice: once without any flags, and a second time with the `--manual-vtable` flag to build and test the alternative implementation.
- **Post-Change Checks:** Tests and pre-commit checks MUST be run after any
  modifications to the codebase.
- **Concept Error Testing:** Use `scripts/test_concept_errors.py` to verify that
  structural mismatches emit precise compile-time errors.

## Critical Paths

- Generation Script: `scripts/generate_protocol.py`
- Proposal Draft: `DRAFT.md`
- Build Entrypoint: `scripts/cmake.sh`

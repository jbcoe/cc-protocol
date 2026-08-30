# Gemini Project Context: protocol

This file provides project-specific mandates and conventions that override
general defaults for this repository.

## Engineering Standards

- **C++ Specification:** Target C++20/26. Prioritize value semantics, type
  erasure, and allocator-aware designs consistent with P3019
  (`std::polymorphic`).
- **Naming Conventions:** Avoid abbreviations in public or internal variable
  names. Use descriptive names like `XYZ_GENERATE_MANUAL_VTABLE` instead of
  `XYZ_GEN_MAN_VT`.
- **WG21 Style:** `DRAFT.md` must adhere to ISO C++ standardization proposal
  norms <https://www.open-std.org/jtc1/sc22/wg21/docs/papers>
- **Paper Format:** We use pure Markdown, no YAML frontmatter, and no HTML blocks.
- **Respect Existing Work:** Assume existing code and comments are well
  considered and meaningful. Avoid churn and needless change; don't rewrite,
  rephrase, or restructure code or comments beyond what a task requires.
- **Solicit Input on Refactors:** Before undertaking any significant refactor,
  ask the user for input and confirmation rather than proceeding unprompted.

## Comments and Commit Messages

- **Why, Not What:** Comments explain the constraint or reasoning behind
  code, not what the code already says. Skip anything a reader gets for
  free from the surrounding code.
- **No Design History:** Describe the current shape of the code and the
  constraint driving it, never a rejected alternative ("rather than X",
  "instead of the Y approach we tried"). A future reader has no context for
  the road not taken.
- **Don't Restate:** Don't repeat a fact already covered by an enclosing
  comment, a nearby comment, or the code directly below it.
- **Tutorials Are a Narrow Exception:** In `tutorials/*.cc`, a WHAT comment
  is earned for genuinely unfamiliar syntax, not for mechanism the adjacent
  code already shows plainly.
- **Regression Guards Point, Don't Narrate:** Point to a durable, resolvable
  reference (bug tracker, repro file) instead of the full history inline.
  Only give the full inline explanation when no durable link exists.
- **Keep Comments Accurate:** Update or remove a comment when the code it
  describes changes; a stale comment is worse than none.
- **Commit Messages Explain Why:** Focus on why a change was made, not what
  changed; the diff already shows what.
- **No Co-Author Trailer:** Do not add a Claude/Anthropic co-author trailer
  to commits. Claude is a tool, not an author.

## Workflow Mandates

- **Tooling:** Always use `uv` for Python dependency management (`uv run ...`).
- **Build & Test:** Use `scripts/cmake.sh` for all build and test operations.
  The `scripts/cmake.sh` entrypoint supports `--debug`, `--release`,
  `--asan`, `--ubsan`, `--tsan`, `--msan`, and `--clang-tidy`.
- **Compiler Preferences:** Prefer Clang 19+ for sanitizer-based verification
  and CI, as it provides superior support for MSAN and TSAN compared to
  older GCC versions.
- **Verification:** All changes must be verified using the `scripts/cmake.sh`
  script to build and test the implementation.
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

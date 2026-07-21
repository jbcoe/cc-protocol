# protocol: project conventions

Project-specific mandates that override defaults for this repo.

## Engineering standards

- C++20/26. Prioritize value semantics, type erasure, allocator-aware design
  (P3019 `std::polymorphic`).
- No abbreviations in variable names (`XYZ_GENERATE_MANUAL_VTABLE`, not
  `XYZ_GEN_MAN_VT`).
- Generated symbols (e.g. vtable entry names) must be deterministic,
  stable, and overload-disambiguating. The Python backend mangles by
  hashing the function signature (MD5); the reflection backend instead
  escapes the signature string byte-for-byte into a valid identifier
  (`escape_to_identifier`), which is injective by construction rather than
  probabilistically collision-resistant.
- `DRAFT.md` follows WG21 proposal norms
  (<https://www.open-std.org/jtc1/sc22/wg21/docs/papers>). Pure Markdown
  only — no YAML frontmatter, no HTML.

## Workflow

- Python deps: always `uv run ...`.
- Build/test only via `scripts/cmake.sh`: `--debug`/`--release`,
  `--asan`/`--ubsan`/`--tsan`/`--msan` (mutually exclusive),
  `--implementation=Python|reflection`. `reflection` needs a P2996 compiler
  (e.g. `CXX=g++-16 CC=gcc-16`) and hard-fails rather than falling back.
- Prefer Clang 19+ for sanitizers/CI (better MSAN/TSAN than GCC).
- While iterating, a targeted compile/test is enough. Before calling a
  change done, run the full suite via `scripts/cmake.sh` plus pre-commit
  checks. If the change touches allocators or manual object lifetime
  (clone/move/destroy, pointer casts), also run `--asan --ubsan`. If it
  touches the vtable registry's shared cache/mutex (`get_mapped_vtable`),
  also run `--tsan`.

## Git

- Never commit, branch, or run history-altering git commands. Ask the human
  if one is needed.

## Critical paths

- Generator: `scripts/generate_protocol.py` · Proposal: `DRAFT.md` · Build:
  `scripts/cmake.sh`

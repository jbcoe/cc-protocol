# Agent instructions

[CONTRIBUTING.md](CONTRIBUTING.md) describes the build, the tests and the
CI checks. The points below are the ones that are easy to get wrong.

- Build and test with `./scripts/bazel.sh`; use `./scripts/cmake.sh` only
  for coverage and clang-tidy. Both scripts select a GCC with C++26
  reflection. A bare `bazel` or `cmake` invocation uses stock GCC and fails
  on `-freflection`.
- Run Python through `uv run`, and run `uv run pre-commit run --all-files`
  after changing anything.
- Do not abbreviate names: `XYZ_PROTOCOL_LIBSTDCXX_DIRECTORY`, not
  `XYZ_PROTO_LIBSTDCXX_DIR`.
- `DRAFT.md` is a WG21 proposal rendered with pandoc: pure Markdown, no YAML
  front matter, no HTML blocks.
- Never rewrite history, force-push, or delete branches.

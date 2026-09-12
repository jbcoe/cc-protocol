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
- No hyperbole, intensifiers, or marketing language ("simply", "just",
  "robust", "powerful", ALL-CAPS) in comments, docs, or PR/commit
  descriptions.

## PR and commit descriptions

- Open with the cause, not "This PR/change...": what was broken, or what
  the old code did, before saying what the diff does about it.
- Name real identifiers, in backticks: files, functions, flags. No
  abbreviations, matching the naming rule above.
- Write prose paragraphs. Use a bulleted list only for a set of unrelated
  discrete changes (e.g. several independent deletions), and a table only
  for measurements.
- Back a performance or behavior claim with a table of real numbers and a
  link to the run that produced them, not an assertion.
- Put `Closes #N.` on its own line; mention related PRs or issues by
  number in an ordinary sentence.
- No boilerplate or hedging.

## Code comments

- Default to no comment. Add one only for what the code cannot show: a
  hidden constraint, an invariant, a workaround for a specific bug.
- Match the code exactly. Check each claim against the header or the
  standard; if a comment and its code drift apart, fix whichever is wrong.
- Describe the code as it stands, never the change that introduced it.
- Write plain declarative sentences: no " -- " or em dash as a connector,
  no rhetorical questions.
- In `tutorials/`, a comment may name the concept being taught.

## Respect the reader's and reviewer's time

- Change only what the task requires. A bug fix does not need
  surrounding cleanup; do not bump versions, rename, reindent, reflow,
  or otherwise tidy code you were not asked to touch. A diff should
  contain only the lines that implement the change. Ask first if a
  drive-by change seems worth doing.
- Before presenting a diff, read the whole thing against the base and
  justify each hunk. Cut anything that is not part of the change you
  set out to make; do not ask the reviewer to spot it for you.
- Keep a PR or commit to one change. Split unrelated fixes into
  separate PRs rather than bundling them because they were found
  together.

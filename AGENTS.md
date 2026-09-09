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

- Default to no comment. Add one only if it states a fact the reader
  cannot get from the code and identifiers at a glance.
- Before keeping a comment, check it does not: restate the
  identifiers or structure, say what something is *not*, repeat a
  comment already given nearby, or hedge ("can be used to") instead of
  stating the verb.
- When a comment is warranted, give the reason a reader could not
  infer: a hidden constraint, an invariant, a workaround for a
  specific bug. One factual sentence is normally enough.
- Match the code exactly. If a comment and the code it describes
  drift apart, fix whichever one is wrong.
- Write plain declarative sentences: no " -- " or em dash as a
  catch-all connector, no rhetorical questions.
- Do not reference the change that introduced the comment ("added for
  X", "fixes #123", "used by the Y flow"). A comment describes the
  code as it stands, not its history.
- Never state a technical claim you have not verified against the actual
  code, API, or standard, no matter why you are touching the comment.
  This applies when avoiding a negative framing invents an unverified
  positive one ("This is an exercise, not recommended practice" has no
  true positive equivalent; do not invent "prefer X instead" — if the
  only true statement is negative, keep it negative). It applies equally
  when replacing vague wording with something more specific: rewriting
  "allocator_traits provides a useful wrapper... an easy way to allocate,
  copy, etc." into "provides allocate, construct, destroy, and deallocate
  regardless of what Alloc defines" replaced filler with a wrong claim —
  allocate/deallocate always forward to Alloc, only construct/destroy
  have real defaults. Check the specific claim, not just the general
  shape of the sentence, against the header or standard before keeping
  it. If the only accurate description is vague, that is what you write.
- A comment may point back to an earlier point in an explicit, ordered
  walkthrough to say what is new or unchanged (e.g. "Same as tutorial 01",
  "unchanged from the section above" ahead of a large near-duplicate
  block). This holds whether the earlier point is in the same file or an
  earlier file the walkthrough names as its prerequisite — a numbered
  tutorial series counts. It does not license referencing an unrelated
  file, or the history of the change that introduced the comment.
- In `tutorials/`, a comment may restate structure or a signature when
  naming the concept being taught is the point of the comment: labelling
  each of the rule-of-five operations in a file about the rule of five,
  or naming which technique a section demonstrates in a file comparing
  techniques. The test is whether the file's own stated purpose is
  teaching a reader to recognize exactly what the comment names, not
  whether the syntax is unfamiliar. The other rules still apply: no
  banned words, and the comment must match the code.
- A comment may flag a fact the reader needs before a later payoff
  elsewhere in the same test, even when that fact is a negation. Example:
  `identity()` tagged `// non-virtual` on three declarations, where a
  later assertion in the same test (`EXPECT_NE(animals[0]->identity(),
  cat.identity())`) only makes sense once the reader knows dispatch on
  `identity()` is not virtual. This is cause stated ahead of its effect,
  not a restatement of the effect, and it overrides the "say what
  something is not" check above for this specific case — that check is
  about a comment with no payoff, not this one. It does not license
  negations in general; most really are noise, and the effect must
  actually appear later in the same test for this to apply.

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

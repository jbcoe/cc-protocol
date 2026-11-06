---
name: review
description: Review a pull request in this repository and write up the review. Use when asked to review a pull request, or to draft or post review comments.
argument-hint: <pull request number>
---

# Pull request review

A review tells the author what to change and why. Everything else in it costs
the reader attention and gives nothing back.

## Find problems

Run `/code-review $ARGUMENTS`. Check each finding against the pull request's
head, compiling a small example where that can settle it, and drop any finding
that does not hold up. Do not dismiss a finding on judgement without checking
it.

## Choose what to raise

Raise a finding only if you can say what goes wrong and it is worth the author's
time to fix. Code that works on main and fails here is always worth raising,
however narrow the case. Leave out:

- Problems that cannot occur in the code under review.
- Hardening in tutorial code that the tutorial is not teaching.
- Refactors that trade repetition for indirection.
- Naming and style preferences that no tool enforces.
- CI and coverage results, which the author can already see.

A confirmed bug that the pull request did not cause stays out of the review too,
but must not be lost: report it when you show the text.

## Write it up

- Say what is wrong and what to change, then stop. Do not restate what the code
  does.
- A finding that changes a contract or needs a design decision is the exception,
  because a short comment makes it look like a small fix. Give the scenario that
  now fails, say what the proposal says about it, and ask who should decide.
- When requesting changes, say which comments block merging.
- Keep a number only if the author would act differently without it.
- Do not say how a finding was checked: no probe output, test counts, flags or
  tool names. Include a reproducer only when the problem is hard to see without
  one, and keep it minimal.
- Attach a comment to the line it concerns. Use the review body only for what
  applies to the whole pull request.
- An approval needs a sentence at most.

| Cut | Write |
| --- | --- |
| After a reload, the lookup returned 0 where the old code returned 7. | The lookup returns a stale entry after a reload. |
| Startup goes from 41 s to 38 s, with all 212 tests passing. | Startup no longer parses the configuration twice. |

A number stays when it is the point: "this adds 200 bytes to every log record,
even with the feature off" gives the author a tradeoff to weigh.

## Post it

Show the person you are working for the full text. With it, list any confirmed
bugs the pull request did not cause, so that they can file issues. Post nothing
until they approve the text.

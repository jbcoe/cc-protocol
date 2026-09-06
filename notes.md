Speeding up scripts/consteval_coverage.py
==========================================

The problem
-----------

scripts/consteval_coverage.py measures which consteval code in protocol.hh
the test suite exercises, by instrumenting a copy of protocol.hh with a
trap call at the start of every block and before every return/throw inside
consteval code, then compiling the test translation units once per probe
line with -fsyntax-only, arming one line at a time. A failed compile (an
uncaught exception during constant evaluation) means the test suite reached
that line. This is O(probe points x translation units) full compiles of a
header that's expensive to parse and instantiate under GCC trunk with
-freflection. On the real repo (79 probe points, 3 translation units, this
sandbox's 2 cores) the baseline run took 3m56s and was fragile: a first
attempt at --jobs 8 on this box was OOM-killed by the kernel, so --jobs
needs to stay well within actual core count.

Baseline result, used throughout as the correctness reference: 77/79 probe
points covered; protocol.hh:166 (a `return false;` in
same_function_with_explicit_object) and protocol.hh:264 (a `throw` in
find_vtable_entry) are never reached by the current test suite.

Approach 1: accumulate-then-report via friend injection (abandoned)
---------------------------------------------------------------------

The first approach tried to collapse the whole probe into one compile per
translation unit. Every trap<Line>() call would unconditionally record
"Line was reached" using the classic friend-injection trick for stateful
template metaprogramming (Filip Roseen's 2015 articles, migrated to
https://refp.se/articles/constexpr-meta-container): a class template
declares a friend function; a different template specialization defines
it; class template instantiation is memoised per translation unit, so the
mark persists across separate constant evaluations even though the
constant evaluator itself cannot leak state between them. A final
static_assert(false, ...) at the end of each translation unit, built with
a computed message (C++26 lets static_assert take a std::string), dumped
every line that had been marked, giving one clean diagnostic per
translation unit.

This worked, and worked correctly: verified against the baseline with a
purpose-built test (a synthetic header with a taken branch, an untaken
branch, and an uncalled function) that also caught a real bug during
development (querying a line's mark before all trap calls for it had
run permanently memoises a false answer - the classic gotcha with this
idiom). End to end it reproduced the 77/79 baseline exactly, in 7.4s
against the real repo, versus 3m56s for the baseline: roughly 32x.

The idiom was abandoned once we thought about it more: it's exactly the
technique described in CWG issue 2118 ("Stateful metaprogramming via
friend injection", https://cplusplus.github.io/CWG/issues/2118.html),
which the core working group has explicitly flagged as arcane and wants
to make ill-formed, with no wording yet to do so. Since this repository is
a reference implementation for a WG21 library proposal, building the
coverage tool - or worse, a tutorial - on a construct the standard
committee is actively trying to prohibit was judged not worth the risk,
however good the speedup. No compiler vendor owes this idiom compatibility.
We considered writing a tutorial about the idiom (tutorials/07_...) since
it's a striking piece of C++, gathered good references for it (Roseen's
original series, the CWG issue, a modern C++20 writeup at
https://mc-deltat.github.io/articles/stateful-metaprogramming-cpp20, and
https://ledas.com/post/857-how-to-hack-c-with-templates-and-friends/), but
dropped the idea for the same reason: not worth teaching a technique the
committee wants to outlaw, in a repo meant to model good practice.

Approach 2: batched round-based probing (adopted)
---------------------------------------------------

The technique actually adopted keeps the original abort-on-uncaught-
exception mechanism (fully standard, no exotic tricks) but exploits
something GCC already does: it does not stop at the first error during
-fsyntax-only. Arm every not-yet-covered line at once instead of one at a
time; every independent top-level constant evaluation the test suite
triggers (roughly, every place code forces protocol.hh's consteval
machinery to run) throws at most once, reporting whichever armed line it
reaches first along its own execution path, and GCC reports every one of
those independent failures in the same compile.

The complication is masking: two armed lines on the same execution path
where one always executes before the other only ever report the earlier
one, since the exception aborts that whole evaluation. The fix is
iterative: after a round, disarm every line it found and recompile; a
masked line surfaces once whatever was masking it is gone. Rounds stop
once one of them finds nothing new. The round count is bounded by the
deepest chain of always-together trap points on a single execution path,
not by the number of probe points - in practice this tracks how deep
protocol.hh's consteval call graph gets, which on the real header took 21
rounds to fully resolve.

Arming many lines at once means many evaluations abort partway through
(e.g. a vtable_generator specialization failing to finish), which cascades
into unrelated-looking downstream errors (incomplete types, missing
members) in the same compile. That's expected noise, not a sign of a
broken instrumenter: only diagnostics matching the trap's own pattern are
trusted, and a batch that fails with zero recognised hits still raises,
preserving the original bug-detection property without needing a separate
"does it compile clean" pass.

Two real bugs were found and fixed while building this:

- The pattern matching the uncaught-exception diagnostic used straight
  quotes, but GCC renders Unicode smart quotes around the exception text
  when the environment's locale allows it (subprocess.run here inherits
  LC_CTYPE=C.UTF-8), so the regex silently matched nothing. Confirmed by
  reproducing it directly - not hypothetical. Investigated GCC's
  -fdiagnostics-format=sarif-stderr as a structured alternative; it gives
  clean per-diagnostic JSON, but the exception text inside message.text is
  still the same prose (and double-escapes the value's braces as JSON
  placeholder syntax), so it doesn't remove the real problem. The actual
  fix: stop encoding the hit line as a runtime value inside braces
  (trap_hit{42}) and instead make it a template argument in the
  exception's own type name (trap_hit<42>). GCC renders that as plain
  digits inside angle brackets regardless of locale, colour, or output
  format, so a dumb substring search is robust by construction. LC_ALL=C
  was also added on the compiler subprocess environment as defense in
  depth.

- The masking algorithm changes how much parallelism is available. The
  original design parallelised across all 79 independent single-line
  probes; batching everything into one compile per translation unit per
  round drops that to a ceiling of (translation units) concurrent compiles
  per round, regardless of core count. Fixed by splitting each round's
  armed set into batches per translation unit, sized automatically from
  the job count (ceil(jobs / num_translation_units) batches per
  translation unit, so total compiles per round is about `jobs`) rather
  than a fixed constant. On this sandbox's 2 cores, with only 3 translation
  units, batching is neutral or slightly negative (confirmed: batch size 4
  took 3m36s and jobs=12 took 3m2s, both slower than no batching's 1m58s,
  since there's no spare core capacity to exploit and extra batches just
  mean extra compiles); the benefit should appear on CI machines with more
  cores than translation units, which is untested here.

Result: verified byte-identical to the baseline (77/79, same two uncovered
lines) at 1m58s against the real repo with --jobs 2, versus the baseline's
3m56s - about 2x, standards-legal, no reliance on anything a future
compiler could legitimately break.

Current state
--------------

The working prototype is scripts/consteval_coverage_v3.py, untracked,
sitting alongside the untouched original scripts/consteval_coverage.py.
Not yet promoted to replace the original. Leftover scratch build
directories from testing: build/consteval-coverage-v3, -v3b, -v3c, -v3d,
-v3e (all untracked, safe to delete).

Open questions for next time: whether to promote v3 to replace
consteval_coverage.py; whether the --lcov-output path (untouched by these
changes, but not re-tested end to end) still works; measuring the batching
benefit on a machine with more cores than translation units.

# P4148R3 outline

Outline for the next revision of `DRAFT.md`, as of 2026-10-10.

## How to read this outline

The recommendation is one main paper, P4148R3, plus two short companions; the
headings below are for P4148R3 and the companions are sketched at the end. Each
heading carries topic bullets and one sentence saying what belongs there, never
paper prose: WG21 does not accept AI-written papers, so the text itself stays a
hand effort.

- **P4148R3 `protocol` and `protocol_view`** (LEWG): the full rewrite tracked in
  #286, restructured on the P3019 template.
- **Why the standard library needs a type-erasing wrapper** (LEWG / SG18): the
  design-independent motivation paper of #287, ideally joint with the proxy
  authors.
- **Implementation experience: type erasure with P2996** (optional,
  informational for SG7 and the P4380 token-injection authors): what C++26
  reflection could and could not generate. SG7 said P4148 itself need not come
  to SG7, so this only earns its place if the gaps list is long enough to
  matter.

Sources: the current `DRAFT.md` (P4148R2; SG18 discussed the paper at Brno in
June 2026), the May 2026 slides, P3019R14, WG21 paper guidance, and every
design issue and PR discussion in jbcoe/cc-protocol. Issue and PR numbers in
brackets point to the discussion behind each bullet.

## 1. Front matter

### Header block

Document number, date, audience, reply-to and project, in the mpark/wg21 header
fields the authors already lean towards (#286).

- Document P4148R3; audience "Library Evolution Incubator (SG18), Library
  Evolution", not R2's "Library Evolution, Library" (SD-7 asks for the right
  audience; the SG7 chair said SG7 need not see it).
- Authors and reply-to as in R2; confirm the list with every co-author before
  the mailing.

### Abstract

One paragraph: structural subtyping at run time, two vocabulary types, value
semantics like `polymorphic`, implementable today with C++26 reflection.

- Drop R2's claim that the design needs code injection; state what the
  reference implementation proves.

### Revision history

One bullet list per revision, newest first, each change tied to committee
feedback where there was some.

- R3: pure C++26 reference implementation (#238, #240); operators, comparisons
  and conversion functions forwarded (#388, #421, #439); implicit views with
  deleted temporaries (#272, #320); interface-controlled movability (#280);
  `protocol_cast` and `target_type` (#306, #323); `valueless_after_move` as a
  hidden friend (#395); formal wording restored (#76).
- R3 corrections to R2's synopsis: remove the default constructor (the
  implementation deletes it, #126), remove the narrowing constructors and defer
  them to a later revision (#88), and drop the "identical constexpr" claim
  (#149).
- R2: fix the duplicated "Add `any`" bullet.
- Brno SG18 (2026-06): record the polls from the paper tracker, then say how R3
  answers them: "Promise more SG18 time" 10/8/6/0/1; "Reflection should be able
  to do something like P4148" 22/2/2/0/0; SG18 asked to see it again alongside
  P3086 proxy; the one strongly-against cited "fundamental requirements that
  cannot be expressed with struct syntax".

## 2. Motivation and scope

### The gap: dynamic structural polymorphism

Keep R2's two-by-two table (nominal/structural × static/dynamic) as the
opening; it is the clearest framing the paper has.

- Concepts give static structural typing; nothing standard gives the run-time
  counterpart.
- Every hand-written erasure re-decides moved-from state, null, SBO and
  allocators, and usually drops allocators (#287).

### Why the standard library, not GitHub

Answer the question that sank proxy at Sofia: an erased wrapper is an interface
type, so two libraries can only meet on it if it is standard.

- The standard library is already a customer: `function`, `move_only_function`,
  `copyable_function`, `function_ref`, `any`, the `shared_ptr` deleter,
  `pmr::memory_resource`, `polymorphic` (#287).
- If the companion motivation paper exists, cite it and keep this subsection to
  a paragraph.

### Use cases

One short worked example each, with a before/after (Tony) table for the two
strongest.

- Heterogeneous collections of values without a base class (Sean Parent's
  document model).
- Retroactive interfaces over types you do not own (Go interfaces, Python
  PEP 544).
- Open extension points, such as LLVM's `PassConcept`/`PassModel`.
- A nameable non-template type as an ABI and compile-time firewall.
- A non-owning view as a function parameter.
- The callable family and overload sets, from R2's function-like table (#27).
- Indexed-container access, from the May 2026 slides.

### Scope

Say what R3 deliberately leaves out, so LEWG polls the MVP rather than the wish
list.

- Out: comparing two protocols (#440), `emplace` (#327), narrowing to a
  different interface (#98), mandated SBO (#148), constexpr calls (#23).

## 3. Prior work and existing practice

R2 compares only against proxy; R3 needs a Prior Work section like P3019's, one
table plus a paragraph on each direct competitor. SG18 wants P4148 seen
alongside P3086, so the proxy comparison is the centre of the section.

| Design | Paper | Ownership | Interface declared by | Status |
| --- | --- | --- | --- | --- |
| [`polymorphic`](https://wg21.link/p3019r14) | P3019 | Owning value | Base class (nominal) | C++26 |
| [proxy](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3086r5.html) | P0957, P3086 | Pointer-like; `proxy_view`, `weak_proxy` | Facade of conventions, with layout limits | No consensus to forward at Sofia 2025 (2/1/4/1/1); C++29 target |
| [Non-Intrusive Interfaces](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4374r0.html) | P4374R0 | Non-owning only | `make_interface<{…}>` with reflections | New in LEWGI, 2026-09; cites neither P4148 nor P3086 |
| `function` family, `function_ref` | N1402, P0288, P2548, P0792 | Owning or non-owning | One call signature | C++11 to C++26 |
| `any` | P0220 | Owning | None; cast back to the type | C++17 |
| `any_view` | P3411 | Owning | Range concept parameters | In LWG for C++29 |
| [Boost.TypeErasure](https://www.boost.org/doc/libs/release/doc/html/boost_typeerasure.html) | none | Owning or reference | Concept list and macros | Boost |
| [Dyno](https://github.com/ldionne/dyno) | none | Storage policy | `DYNO_INTERFACE` | Experimental |
| [folly::Poly](https://github.com/facebook/folly/blob/main/folly/docs/Poly.md) | none | `Poly<I>` value, `Poly<I&>` reference | Nested `Interface` template plus `Members` | Production |
| Metaclasses `interface` | P0707R5 | n/a; generates a base class | Metafunction (nominal) | Waits on generative reflection |
| rjk::duck | none | Owning | Struct, reflection | Basis of the reference implementation (#94) |

- **proxy:** facade versus plain struct, `p->f()` versus `p.f()`, layout in the
  type versus QoI, erased versus explicit ownership; rewrite the AI-generated
  points from #26 by hand and add downgrading and `weak_proxy`.
- **P4374:** new since R2; compare its non-owning reference with
  `protocol_view` and its tokens with struct syntax.
- **Other languages:** Go interfaces (implicit, structural), Swift
  existentials, Rust `dyn Trait` (nominal), Python PEP 544 (static only); one
  sentence each on what C++ can borrow.
- **Sean Parent's concept/model idiom and External Polymorphism:** the
  hand-written pattern `protocol` generates (#414).
- **Still to read:** Quuxplusone's `std::function` design space (#463) and
  Eduardo Madrid's zoo jump tables (#399, #462).

## 4. Design overview

### A first example

Open with one interface struct, two unrelated conforming types, a `protocol` in
a `vector` and a function taking a `protocol_view`, before any rules.

### Interfaces

An interface is a plain class whose public non-static member functions are the
contract; say once what kinds of member are forwarded, rejected or ignored.

- A valid interface is a non-cv, non-ref class type with no volatile members
  (#162).
- Forwarded: named functions and overloads, call operators, the operators
  listed in the operators section, conversion functions (#209, #210, #388,
  #421, #439).
- Rejected with a diagnostic naming the member: member templates,
  `operator auto`, `operator=`, `co_await`, unary `&`, comparisons with `I`,
  reserved names, ref-qualified and explicit-object members, for now (#406,
  #434, #448, #449).
- Ignored: statics, including allocation operators, and hidden-friend
  comparisons (#447).

### Conformance

A type conforms when, for each interface member, it has a member with the same
name and exact signature; noexcept is one-directional (#23).

- `protocol` checks the decayed type because it copies; views check the
  cv-qualified type (#198).
- Candidates may use deducing `this` and static member functions (#274).

### `protocol<I, Allocator>`

An owning value type modelled member-for-member on `polymorphic`: deep copy,
const propagation, allocator support, valueless after move.

- Constructors: explicit, because they allocate; in-place and
  allocator-extended forms as in P3019 (#272).
- Special members follow `I`: copyable or move-only interfaces give copyable or
  move-only protocols (#280).
- Accessors as hidden friends: `protocol_cast<T>`, `target_type`,
  `valueless_after_move` (#306, #323, #395).

### `protocol_view<I>` and `protocol_view<const I>`

A trivially copyable two-pointer reference type, like `span` and
`function_ref`.

- Shallow const: every forwarded member of `protocol_view<I>` is const;
  `protocol_view<const I>` exposes only the const members (#186, #196).
- Constructs implicitly from conforming objects and from `protocol`;
  construction from temporaries is deleted (#270, #285, #320).
- `protocol_view<I>` converts to `protocol_view<const I>` (#229, #244).

### Calling through the wrapper

Forwarded members are called as `p.draw()`, so a `protocol<I>` reads like the
object it holds; explain early that this puts every library-declared member
into the interface's namespace, which drives the member-versus-hidden-friend
choices later.

## 5. Design decisions and alternatives

One subsection per decision, each giving the choice, the alternatives and why
they lost, in P3019's order where it applies. P3019 also ended with a table of
decision, alternative, and whether it could change later without breaking
code; reuse that as an appendix.

### Modelled on `polymorphic`, and where it departs

List what `protocol` copies from `polymorphic` (deep copy, const propagation,
allocators, valueless after move, explicit allocating constructors) and every
departure, each with its reason.

- Departures: no `operator*`/`operator->` of its own; `valueless_after_move` is
  a hidden friend (#395); forwarded `operator()` and `operator[]`, which LEWG
  declined to add to P3019 (0/0/10/4/5); move with equal allocators must steal
  the pointer (#194).
- Also compare with `function_ref` (implicit views, but deleted rvalues),
  `span` (shallow const) and `any` (casts and `target_type`).

### Interfaces as plain structs

Why a struct, not a concept, a facade or a metaclass: any existing type,
including third-party ones, can be the interface, and non-experts can write
one.

- Answer Brno's objection about requirements a struct cannot express: free
  functions, associated types, templates, operators between two `I`s.

### Member-call syntax and reserved names

`p.f()` makes a `protocol` a drop-in for the objects it holds, at the price
that every library-declared name competes with interface names.

- Consequences: hidden-friend accessors (#306, #323, #395), no `emplace`
  (#327), reserved `allocator_type`, `get_allocator`, `swap` (#449).
- `swap` stays a member for parity with `polymorphic`; cite LWG4250, closed NAD
  in Brno (#194).
- Still to settle: private helpers hiding interface names, and one rule for
  member versus hidden friend (#450–#453).

### The conformance rule

Conformance is declaration-based and exact; say why, and which relaxations
could come later without breaking code.

- Exact return types, reversing an early covariant design (#17, #101).
- noexcept is one-directional; const and `explicit` currently must match
  exactly, both questioned (#296, #426).
- Partly forced by C++26: without identifier splicing, "is `t.f(args...)`
  valid?" cannot be asked (#29).
- Alternatives: implicit conversions (#121), template candidates via an exact
  instantiation (#393).

### Construction and special members

The interface decides copyability and movability, and `protocol` has no
default constructor.

- Copyable `I` needs copyable `T`; move-only `I` gives a move-only `protocol`
  (#280).
- No default constructor, since it would make "valueless after move" a misnomer
  (#126, #151); R2's synopsis still shows one.
- Allocators all-or-nothing from the first version, since LEWG said they cannot
  be added later (P3019).

### The valueless state

The one known correctness hole; the paper must say what every operation on a
moved-from `protocol` does, as P3019 had to.

- Calling a forwarded member is a precondition violation (#208); a view of a
  valueless `protocol` currently calls a null pointer (#284).
- A forwarded `explicit operator bool` makes `if (p)` look like an emptiness
  test (#427).
- Options: undefined behaviour, a hardened precondition as P4349 proposes for
  `polymorphic`, or `terminate` (#305).

### Views: implicit, shallow const, no temporaries

Views convert implicitly because they never allocate; `protocol` stays explicit
because it does (#272, #320).

- Shallow const like `span` (#186, #196), with its knock-on effects on
  const/non-const overload pairs and conversion functions (#220, #391, #438).
- Construction from rvalues deleted, following `span` rather than
  `function_ref`; record the two lifetime traps that remain (#75, #272, #285).
- Must stay trivially copyable, so it passes in registers (#212, #215).

### Operators and conversion functions

State which operators are forwarded, which are rejected and why (`=`,
`co_await`, unary `&`, binary `*`), and that the list is hand-written on
purpose (#388, #367).

- Comparisons with other types are forwarded; comparisons with `I` are
  rejected, which keeps `p == q` free for later (#439, #440).
- Conversion functions forwarded; `operator auto` rejected (#421, #448).
- Right-hand-side operators such as `os << p` have no design yet (#100).

### Value categories and ref-qualifiers

Ref-qualified interface members are rejected today; the paper needs one rule
for when a candidate conforms in each value category (#97, #268, #356).

- Explain the [over.load] limit on which overload sets an interface can
  declare.

### Conversion between interfaces

Recommendation: drop R2's narrowing from R3 and defer it to a later revision,
since nothing implements it and its vtable layout is undesigned (#88, #98,
#466).

- P3019 refused conversions between `polymorphic` types for lack of a use case;
  narrowing needs a stronger case.
- `protocol<J>` as a conforming type for `I` chains vtables without bound
  (#244, #262).

### Type access, RTTI and allocation operators

`protocol_cast<T>` and `target_type` follow `any` and need RTTI; allocation
operators on `I` are ignored because the allocator is the only control (#306,
#323, #447).

### constexpr, SBO, hash and format

Answer each LEWG checklist item, even where the answer is no.

- Remove R2's "identical constexpr" claim: calls cannot be constant expressions
  until C++29 allows the thunk technique (#149, #23); constructors can be
  (#236).
- SBO is quality of implementation, as for `polymorphic`, and must keep the
  valueless-after-move rule (#148, #465).
- No `hash`, `formatter` or comparison of two protocols in this version.

### Alternatives considered

Keep R2's subsection and add what the repo has since tried.

- Relaxed structural subtyping; concepts as interfaces; virtual dispatch
  instead of a hand-built vtable (dropped after near-parity benchmarks, #34,
  #58); one class template instead of two.

## 6. Impact, language dependencies and implementation experience

### Impact on the standard

A pure library addition: a new header `<protocol>`, two class templates, a
feature-test macro, and nothing changed in existing components.

- Name the header and the `__cpp_lib_protocol` macro (SD-6).
- Argue the header: SG18 asked where proxy's facilities should live, noting
  that `polymorphic` went into `<memory>`.
- Freestanding: say whether `protocol_view`, which never allocates, can be
  freestanding.
- Incomplete types: `polymorphic` accepts an incomplete `T`, but reflecting
  over `I` needs a complete `I`; state the difference.

### What the language must provide

Correct R2: C++26 reflection is enough to implement the design, and token
injection (P3294, now P4380R0 in EWG for C++29) would only make it cleaner
(#29).

- Used: `define_aggregate` for the vtable and the per-member thunks,
  `substitute`, `template for` and splices.
- Gaps hit: no identifier splicing, which forces declaration-based conformance;
  attributes and undeduced `auto` are invisible to `members_of` (#163, #448);
  no reflection of using-declarations.
- A standard implementation may use compiler support instead; the
  specification must not depend on the thunk technique.

### Observable artefacts of a library implementation

List what users can see of the "vanishing `this`" technique and decide which
the wording permits.

- Forwarded members are empty data members with call operators, so
  `&protocol<I>::f` and `auto g = p.f;` differ from a real member function;
  copying a thunk is blocked (#202, #215).
- The view stays trivially copyable at two pointers, so it passes in registers
  (#215).

### Implementation experience

One reference implementation, the sole one, in pure C++26 with tests,
sanitizers and coverage; say where it builds and what it does not yet do.

- GCC trunk with `-freflection` (Linux, macOS); clang-p2996 for clang-tidy
  only, with upstream bugs tracked in #277.
- Compile time: about 30 ms per interface member across `protocol` and both
  views (#423).
- Run-time cost: a function-pointer call with `noexcept` carried into the entry
  type (#385); benchmarks against virtual dispatch and proxy are still to do
  (#294).
- Not yet: narrowing between interfaces (#98, #466), constexpr constructors
  (#236), ref-qualifier propagation (#97, #356), a second implementation by
  token injection (#154).

## 7. Polls and open questions

Rewrite R2's two question-form polls as statements that change the status quo,
following
[LEWG's Writing-Polls guidance](https://github.com/cplusplus/LEWG/wiki/Writing-Polls),
and give each open question a recommended answer.

### Direction polls for SG18

Ask for direction first, so the design polls only matter if the direction
passes.

- SG18 prefers struct-declared interfaces with member-call syntax (P4148) over
  an explicit facade (P3086) for standard structural polymorphism.
- SG18 wants an owning and a non-owning type, not a non-owning one only (versus
  P4374).
- Forward P4148 to LEWG with the changes polled below.

### Design polls

One poll per decision the authors want the room to own; state the authors'
recommendation beside each.

- Conformance is by declaration (exact signature), not by expression validity
  (#29, #121, #393).
- No conversion between `protocol`s of different interfaces in the first
  version (#98, #255, #262, #390).
- Library-declared operations that need no member are hidden friends, and
  interfaces may not declare the names `protocol` reserves (#449, #452, #453).
- `protocol` gets a constrained converting assignment `p = t;` (#322).
- Ref-qualified interface members propagate their qualifiers (#97, #268,
  #356).
- `protocol<I>` supports only class-type interfaces (#300).

### Questions to settle before R3, not to poll

Author decisions LEWG will expect to see made, each with a short rationale in
the paper.

- What calling through a moved-from `protocol`, or a view of one, does,
  including a forwarded `explicit operator bool` (#284, #427); align with
  P4349's hardened preconditions for `polymorphic`.
- Private or protected interface members: reject or ignore (#451).
- Right-hand-side operators such as `os << p` (#100).
- Whether a candidate's `explicit` conversion must match the interface's
  exactly (#426).
- Comparing two `protocol`s stays out of scope; say so in the paper and close
  #440.

## 8. Proposed wording

Restore formal wording (#76), modelled clause-for-clause on the
`indirect`/`polymorphic` wording that P3019R14 added to [mem]; the novel part
is specifying members generated from `I`.

- **[version], [headers]:** add `<protocol>` and `__cpp_lib_protocol`.
- **Exposition-only definitions:** _valid-interface_,
  _interface-member-functions_(I) and _conforms-to_(T, I), defined on
  declarations, not on expressions.
- **Forwarded members:** one paragraph saying that for each interface member,
  `protocol<I>` and `protocol_view<I>` have a member with the same name,
  parameters, noexcept and the stated const rule, with _Preconditions_ and
  _Effects_ written once.
- **Class template `protocol`:** synopsis, then _Mandates_, _Constraints_ and
  _Effects_ per constructor, assignment, swap and accessor, copying P3019's
  style; the T-versus-I rule for special members as _Constraints_ (#194).
- **Class template `protocol_view`:** synopsis for `I` and `const I`,
  constructors, the deleted rvalue overloads and the trivially-copyable
  guarantee.
- **Hidden friends:** `protocol_cast`, `target_type`, `valueless_after_move`,
  and the exception type `protocol_cast` throws (#279).
- **Reserved names:** the rule that makes an interface ill-formed when it
  declares a name the class template reserves (#449, #452).
- **Leave out:** name mangling, vtable layout and SBO; say they are
  implementation details (#259, #148).

## 9. Back matter and companion papers

### Appendices to P4148R3

Move the long material out of the body, as P3019 did, so the design section
reads in one sitting.

- **A. Before/after tables:** the standard function wrappers, `any` and
  `any_view` written as `protocol` and `protocol_view`; SG18 asked proxy for
  exactly this at Croydon (#27, #91).
- **B. Decision table:** each decision, its alternative, and whether changing
  it later would break code.
- **C. Limitations of the reference implementation:** what is rejected, what is
  silently ignored (`auto f() const;`), and mangling limits (#256, #407).
- **D. Illustrative implementation:** replace R2's hypothetical-primitive
  appendix with a short tour of the real C++26 one.
- **E. FAQ:** the Brno objection, "why not virtual functions", "why not proxy",
  "why not a concept".
- **F. Forwarding checklist answers:** answer each item of the
  [LEWG paper-forwarding checklist](https://github.com/cplusplus/LEWG/wiki/Paper-Forwarding-Checklist)
  explicitly, as P3019 did: why std and which header, prior art, examples,
  implementation and deployment experience, performance, format, hash and
  allocators, feature-test macro, constexpr, freestanding, changes LEWG asked
  for, wording.

### Acknowledgements and references

Thank the BSI panel, Brno SG18, and contributors whose design arguments shaped
the paper; fix the `[P3086R4]` label, which points at the R5 URL.

- Contributors to name, with their agreement: RyanJK5 (rjk::duck), Dooez,
  hanickadot, Twon, nbx8, Billy Baker, Tony van Eerd.
- References: P3019R14, P0201, P1950, P2996R13, P3294, P4380, P3086R5,
  P4374R0, P4349, P0792, P2548, P3411, PEP 544, LWG4250.

### Companion paper: why the standard library needs a type-erasing wrapper

A short, design-neutral paper answering "why std, not a package manager", which
is what stalled proxy at Sofia (#287).

- Interfaces as meeting points between libraries; the standard library as an
  existing customer; seven use cases with prevalence data.
- Performance evidence: compare with virtual dispatch and with proxy (#294).
- Co-author with the proxy authors if they agree, so SG18 sees one motivation
  for both designs.

### Optional companion: type erasure with C++26 reflection

An informational experience report on what P2996 could and could not do, for
SG7 and the P4380 authors.

- Gaps: no member-function synthesis (`define_aggregate` makes data members
  only), no identifier splicing, no closure-type or explicit-object queries,
  undeduced `auto` hidden from `members_of`, no reflection of attributes or
  using-declarations.
- Compiler bugs found and fixed upstream, as evidence the facility was
  exercised (#277, #455).
- The hand-written operator thunks show the limit of C++26 reflection (#388).

## 10. Sources

All 153 issues and 298 PRs in jbcoe/cc-protocol were triaged on 2026-10-10; the
89 issues and 182 PRs that bear on the paper were read in full, comments and
reviews included. The table maps each outline section to the threads to reread
when writing it.

| Section | Issues | PRs |
| --- | --- | --- |
| Front matter, process | #76, #286, #396 | #68, #77, #83, #88, #457 |
| Motivation | #27, #287, #294 | #69, #91, #233 |
| Prior work | #26, #94, #154, #399, #414, #461–#463 | #73, #83 |
| Interfaces, conformance | #29, #121, #162, #163, #268, #393, #407, #426, #451 | #101, #256, #274, #275, #296, #434, #436 |
| Names and accessors | #194, #279, #314, #318, #327, #394, #450, #452, #453 | #306, #323, #395, #449 |
| `protocol` semantics | #148, #187, #193, #208, #284, #300, #322, #427, #440 | #126, #140, #253, #280, #305, #378, #465 |
| `protocol_view` | #186, #270, #285, #372 | #64, #75, #174, #196, #204, #212, #215, #272, #320, #343 |
| Operators, conversions | #98, #100, #255, #262, #390 | #244, #367, #388, #391, #421, #433, #438, #439, #448, #466 |
| Value categories, constexpr | #23, #97, #200, #236 | #149, #260, #356, #459, #464 |
| Implementation, compilers | #2, #122, #202, #277, #423, #455 | #34, #58, #95, #191, #385, #425, #454 |

### WG21 references

- [P3019R14 `indirect` and `polymorphic`](https://wg21.link/p3019r14), and its
  [paper tracker](https://github.com/cplusplus/papers/issues/1680) for the LEWG
  poll history.
- [P4148 tracker](https://github.com/cplusplus/papers/issues/2724),
  [P3086R5 proxy](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3086r5.html),
  [P4374R0 Non-Intrusive Interfaces](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/p4374r0.html).
- [SD-7 how to write papers](https://isocpp.org/std/standing-documents/sd-7-mailing-procedures-and-how-to-write-papers),
  [SD-9 LEWG policies](https://isocpp.org/std/standing-documents/sd-9-library-evolution-policies),
  [SD-6 feature-test macros](https://isocpp.org/std/standing-documents/sd-6-sg10-feature-test-recommendations),
  [P2138R4 design and wording](https://wg21.link/p2138r4).
- [LEWG paper-forwarding checklist](https://github.com/cplusplus/LEWG/wiki/Paper-Forwarding-Checklist),
  [LEWG Writing Polls](https://github.com/cplusplus/LEWG/wiki/Writing-Polls),
  [mpark/wg21 tooling](https://github.com/mpark/wg21).

# Plan: implement `protocol_view` and `protocol` with C++26 reflection

## Context

`xyz::protocol<T,A>` and `xyz::protocol_view<T>` (DRAFT.md) are currently
implemented by a Python/libclang codegen step (`scripts/generate_protocol.py` +
`scripts/protocol.j2`) that emits a per-interface header. DRAFT.md's own ideal
implementation uses static reflection instead, removing the codegen step
entirely. `tutorials/1_type_erasure.cc`, `2_vanishing_this_pointer.cc`, and
`3_reflection.cc` build up the primitives this plan uses directly: plain type
erasure, the "vanishing this" owner-recovery technique via a single-member base
struct, and reflection facilities including `define_aggregate`,
`data_member_spec`, and `substitute` over a computed type-list.

## Plan

1. Add the `protocol_view` class template: constructors from an implementation
   object, copy/move semantics, and a plain `void*` to the implementation as
   storage. This step uses ordinary C++, giving later steps a class to extend.
   Its tests cover constructibility and copy/move behaviour.

2. Use reflection to build a conformance check that constrains step 1's
   implementation-object constructor. For each interface member function, every
   same-named member function on the implementation type is reflected directly
   and compared against the interface member's declared signature, requiring an
   exact match (`std::same_as`) on every parameter type and the return type; a
   member is modeled if any same-named candidate matches exactly. The result is
   wrapped in a `conforms_to<Implementation, Interface>` concept, added to step
   1's constructor via a `requires` clause. Tests cover a conforming type
   constructing successfully and non-conforming types (missing a member, a
   parameter or return type that differs from the interface's) failing to
   compile with a diagnostic naming `conforms_to`.

3. Use reflection to synthesize member functions that throw. Built against a
   small throwaway interface, declared specifically for this and the following
   steps, with several methods, mixed const and non-const, so later steps have
   real distinct cases to generalize across. Reflection synthesizes a named,
   callable data member for each interface method; each one's body throws
   `unimplemented_code`, proving the synthesis produces the right named,
   callable shape before step 4 onward wires up real dispatch.

4. Add a vtable using reflection, extending `protocol_view`'s storage from step
   1 with a vtable pointer alongside the implementation pointer. The vtable's
   shape is built via `substitute`-driven type synthesis over the interface's
   members, independent of any dispatch logic, and is testable purely on the
   shapes it produces.

5. Wire dispatch for the fixture interface's methods, using the vanishing-this
   trick to route them through the real vtable from step 4, retiring
   `unimplemented_code` and its throwing bodies entirely. The forwarder design
   from step 3, the vtable from step 4, and a minimally complete class body are
   mutually dependent, making this commit the plan's natural largest unit of
   review. Overload resolution is a separate axis of difficulty, handled in step
   6.

6. Support overloads via duck-typed dispatch: candidate implementations are
   merged so the compiler's own overload resolution, rather than a hand-rolled
   equivalent, decides which overload is callable.

7. Add `protocol_view<const T>`. Mechanically low-cost once steps 1-6 exist,
   since `protocol_view` and `protocol_view<const T>` share almost all their
   constructor shapes; this step differs mainly in const-qualifying access to
   the underlying implementation.

8. Add narrowing conversions between `protocol_view` specializations, proving
   the narrowing-conversion machinery (constructing a `protocol_view<Base>` from
   a compatible `protocol_view<Derived>`) before `protocol` exists. `protocol`'s
   own narrowing is step 11.

9. Add `protocol`, the owning type: `protocol<T>`, covering ownership and
   lifetime management (construction from an implementation, copy/move,
   destruction). Step 12 extends the class template with an `Allocator`
   parameter, changing every constructor and converting-constructor signature
   declared here.

10. Add `protocol_view`'s converting constructors from `protocol`:
    `protocol_view(protocol<I,Alloc>&)` and, for `protocol_view<const I>`,
    `protocol_view(const protocol<I,Alloc>&)`, per DRAFT.md's specification of
    `protocol_view` as referencing either an implementation directly or a
    `protocol<I,Alloc>`. Kept as its own step, with its own tests, separate from
    step 9, so that step stays focused on `protocol` itself.

11. Add narrowing conversions for `protocol`, covering `protocol`-to-`protocol`
    and `protocol`-to-`protocol_view` narrowing and completing the symmetry that
    DRAFT.md specifies across both types (step 8 covered only
    `protocol_view`-to-`protocol_view`).

12. Add allocators for `protocol`: the `Allocator` template parameter,
    throw-safe allocate-then-construct-or-deallocate behaviour, and `protocol`'s
    allocator-extended and allocator-converting constructors.

13. Add operator forwarding for interface `D`: generate forwarding for `D`'s 38
    operators (`operator+`, `operator[]`, and the rest), each with its own
    single-member base struct combined by multiple inheritance, wired into
    `protocol`/`protocol_view` alongside the named forwarders from steps 3-6.
    Tests cover the generated forwarding directly and enable interface `D`'s
    existing operator tests.

## Verification and invariants

The one-base-struct-per-member, multiple-inheritance structure used for operator
forwarding (step 13) is a project-wide invariant for this backend: re-check it
wherever a new forwarding or wrapper pattern is introduced.

This backend relies on pointer casts and object-layout tricks, exactly what
sanitizers are best at catching. Run at least one sanitizer (`--asan` or
`--ubsan`) at each of the major dispatch-wiring milestones (steps 5, 12, and
13), in addition to the ordinary `./scripts/cmake.sh --reflection`
build-and-test cycle every step requires.

Every step should build and pass its own tests with
`XYZ_PROTOCOL_USE_REFLECTION` both `ON` and `OFF`, confirming zero effect on the
existing codegen backend when the option is off.

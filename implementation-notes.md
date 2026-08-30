# C++ Protocol Reference Implementation Notes

This document describes the C++26-reflection-based implementation of
`protocol` and `protocol_view` in `protocol.hh` (namespace
`xyz::reflection`).

---

## 1. Interface Conformance

An interface is a plain struct or class. `conformance_candidate_infos`
collects a type's named, non-special member functions and call operators,
static or not, via `members_of` filtered by `std::meta::is_function`. (GCC16's
`members_of` does not yet enumerate a closure type's call operator, so for a
lambda the operator is named directly as a fallback.)
`protocol_interface_functions_of` keeps only the non-static entries of that
list — the member functions an interface requires. An entry's index in this
array is also its vtable slot.

`is_protocol_conformant<Interface, Candidate>` checks that every interface
member function has a matching candidate member, via
`member_function_conforms_to`:

- the same name, or, for `operator()`, being a call operator on both sides
- the same de-aliased return type and parameter types
- the same reference qualifiers (for a non-static candidate)
- const: the candidate must be const if the interface member is
- `noexcept`: the candidate must be noexcept if the interface member is

A static candidate has no object parameter, so it satisfies any const or
reference qualification the interface member declares. Static member
functions declared on the interface itself are not required of a candidate.

The check is O(N\*M) over interface and candidate member counts, assumed
negligible at compile time.

## 2. Vtable Generation

`vtable_generator<T>` builds, via `define_aggregate`, a struct of function
pointers — one per entry of `protocol_interface_functions_of<T>`
(`generate_vtable_specs`). A pointer's signature is `R(*)(void*, Args...)`
for a mutable interface member or `R(*)(const void*, Args...)` for a const
one. Entries are named by `vtable_entry_name`: the member's name (or
`call_operator`) plus its index, so overloads sharing a name still get
distinct entries.

`make_view_vtable<T, U, ConstPolicy>` populates a `vtable_generator<T>::vtable`
for a concrete type `U`: each entry is a trampoline
(`mutable_view_trampoline`/`const_view_trampoline`) that casts the
type-erased pointer back to `U*`/`const U*` and calls the member of `U` found
by `find_conforming_member`. Under `const_policy::const_only`, only entries
for `T`'s const members are populated.

`protocol<I, Alloc>` extends this with its own `vtable`, which derives from
`I`'s view vtable and adds `destroy`, `copy` and `move` function pointers
(allocator-aware, built in `make_vtable_for`) used for ownership. A valueless
(moved-from) `protocol` points at a `null_vtable` whose `destroy`/`copy`/
`move` are no-ops and whose member-function entries are left null; calling a
member function on it is a precondition violation.

## 3. Member Function Thunks

Calling `p.member(args...)` is provided by a generated wrapper base, one per
interface member function name, produced by `member_base_generator` /
`generate_wrapper_bases` and combined into `protocol_wrappers_t`, which
`protocol` and `protocol_view` inherit from.

Each base holds a `member_thunk`: one `method_thunk` per overload (an
`operator()` mirroring that overload's signature), with every overload's
`operator()` brought into scope so ordinary overload resolution applies as it
would on the interface itself. The call-operator case (`operator()`) is
handled the same way through `call_operator_base`.

A thunk recovers the enclosing `protocol`/`protocol_view` object with a
"vanishing this pointer" cast (`method_thunk::enclosing`), then dispatches
through the matching vtable entry:

```cpp
R operator()(Args... args) noexcept(IsNoexcept) requires(!IsConst) {
  auto* protocol_object = static_cast<ProtocolType*>(enclosing(this));
  const Vtable* vtable = protocol_object->vtable_;
  return vtable->[:entry:](protocol_object->object_,
                           std::forward<Args>(args)...);
}
```

Thunks are private-defaulted (constructible/copyable only by their
`member_thunk`/`call_operator_base`), and `protocol`/`protocol_view` each
`friend` `method_thunk` to grant it access to their `object_`/`vtable_`.

Where a const/non-const overload pair would collide once every wrapper is
const-qualified (`protocol_view`'s shallow const), `generates_wrapper_for`
drops the const overload, since a non-const reference would resolve to the
non-const one anyway.

## 4. Ownership and Allocators

`protocol<I, Alloc>` stores a type-erased `object_` pointer, a `vtable_`
pointer and an `[[no_unique_address]] Alloc alloc_`. Construction rebinds
`Alloc` to the stored (decayed) type and allocates/constructs through
`allocator_traits`. Construction from any conforming `T`, allocator-extended
construction, and in-place construction (`std::in_place_type_t`, including an
initializer-list overload) are all provided, each with an allocator-extended
counterpart.

Copy construction/assignment go through the vtable's `copy` entry and are
constrained on `std::is_copy_constructible_v<I>`. Move construction/
assignment move the object pointer directly when the allocators compare
equal or `allocator_traits::is_always_equal`, and otherwise heap-allocate in
the target allocator and move-construct via the vtable's `move` entry,
honouring `propagate_on_container_copy_assignment`,
`propagate_on_container_move_assignment` and `propagate_on_container_swap`
respectively. `swap` asserts the allocators are equal when neither
`is_always_equal` nor propagation on swap holds. `valueless_after_move()`
reports whether the stored pointer is null.

## 5. `protocol_view<T>` and `protocol_view<const T>`

`protocol_view<T>` is a non-owning `void*`/vtable-pointer pair, constructible
from any non-const `U` conforming to `T`. It is shallow-const: its wrappers
are generated with `const_policy::all_const`, so every member is exposed as a
const member function of the view and `const protocol_view<T>` does not
restrict the interface, mirroring `std::span`.

`protocol_view<const T>` is a `const void*`/vtable-pointer pair constructible
from a (possibly const) `U`, generated with `const_policy::const_only`: only
`T`'s const member functions get wrappers and vtable entries; the rest are
never called through it.

Both specializations share one vtable per `(T, U, ConstPolicy)` combination
(`view_vtable_for`), delete the default constructor, and default the
remaining special member functions.

## 6. Limitations

- No operators other than `operator()` are supported.
- Member function templates are not matched as conformance candidates.
- Conformance checking accounts for an interface member's lvalue/rvalue
  reference qualifier, but the generated call wrapper does not itself apply
  the qualifier: `method_thunk`'s `operator()` overloads are unqualified.
- There is no conversion between a `protocol`/`protocol_view` of one
  interface and a `protocol`/`protocol_view` of another.
- Conformance checking (`is_protocol_conformant`) is O(N\*M) over interface
  and candidate member counts.

# `protocol`: Structural Subtyping for C++

ISO/IEC JTC1 SC22 WG21 Programming Language C++

D4148R0

Working Group: Library Evolution, Library

Date: 2026-03-25

_Jonathan Coe \<<jonathanbcoe@gmail.com>\>_

_Hana Dusikova \<<hanicka@hanicka.net>\>_

_Antony Peacock \<<ant.peacock@gmail.com>\>_

_Philip Craig \<<philip@pobox.com>\>_

## Abstract

We propose `protocol` and `protocol_view`, standard library vocabulary
types for structural subtyping in C++. Interfaces are specified as plain structs;
any type whose member functions satisfy the interface is accepted without
requiring explicit inheritance. The owning type, `protocol`, provides value
semantics with deep-copy behaviour. The non-owning type, `protocol_view`,
provides a lightweight reference to any conforming type, analogous to
`std::span`. Both types are generated automatically by the compiler using static
reflection, eliminating hand-written type-erasure boilerplate.

## History

### Changes in revision R0

- Initial revision.

## Motivation

Traditional polymorphism in C++ requires inheritance from a common base class.
This coupling prevents the retroactive application of interfaces to existing
types and mandates reference semantics, which complicates ownership reasoning.

`std::function` provides structural subtyping for single callable objects, but
no standardised equivalent exists for interfaces comprising multiple member
functions. Developers must author bespoke type-erased wrappers, incurring
substantial boilerplate for vtable construction, storage management, and
const-propagation. Such wrappers are difficult to write correctly and must be
duplicated for every interface.

Structural subtyping is well-established in other languages. PEP 544 introduced
Protocols in Python: a class that structurally provides the required methods is
considered a subtype without explicit inheritance. We propose an equivalent
mechanism for C++.

We propose `protocol<I>` to own an object of any type that satisfies interface
`I`, providing value semantics and deep copy. We propose `protocol_view<I>` to
refer non-owingly to any conforming type, enabling efficient observation at
function boundaries without allocation or ownership transfer.

Nominal subtyping allows non-owning polymorphism via raw pointers or references
(`Base*`, `Base&`). Structural subtyping has no native equivalent. `protocol_view`
fills this gap and is to `protocol` as `std::span` is to `std::vector`.

## Examples

The following examples illustrate the use of `protocol` for value-semantic
ownership and `protocol_view` for non-owning observation. Note that `Circle`
does not inherit from `Drawable`; it satisfies the interface structurally.

```cpp
struct Drawable {
  std::string_view name() const;
  void draw();
  int draw_count() const;
};

struct Circle {
  std::string_view name() const { return "Circle"; }
  void draw() { ++draw_count_; }
  int draw_count() const { return draw_count_; }

private:
  int draw_count_ = 0;
};
```

### `protocol` and value semantics

`protocol<I>` owns its contained object. Copying a `protocol` performs a
deep copy of the underlying object.

```cpp
// Construct in-place
protocol<Drawable> p1(std::in_place_type<Circle>);

// p2 is a deep copy of p1, including the underlying Circle object
protocol<Drawable> p2 = p1;

p1.draw();
p1.draw();

// p1 and p2 are independent
assert(p1.draw_count() == 2);
assert(p2.draw_count() == 0);
```

### `protocol_view` and reference semantics

`protocol_view<I>` is a non-owning view of any type satisfying interface
`I`. Copying a `protocol_view` is a shallow operation; both copies refer to the
same underlying object.

```cpp
void print_name(protocol_view<const Drawable> view) {
  // A const view permits only const member functions.
  std::cout << "Name: " << view.name() << "\n";
}

Circle circle;

// Bind a view to a concrete object without allocation or ownership transfer.
print_name(circle);

protocol<Drawable> p(std::in_place_type<Circle>);

// Bind a view to an owning protocol object.
print_name(p);

// Copying a view is shallow; both views refer to the same Circle.
protocol_view<Drawable> v1(circle);
protocol_view<Drawable> v2 = v1;
v2.draw();
assert(circle.draw_count() == 1);
```

## Design

### Requirements

We require the following properties of `protocol` and `protocol_view`.

A type satisfies an interface based solely on the presence of member functions
with conforming signatures; explicit inheritance is not required.

`protocol<I>` provides value semantics. Copying a `protocol` object performs a
deep copy of the underlying erased object. Moving a `protocol` object leaves it
in a valid but unspecified (valueless) state, enabling efficient move operations
without heap allocation.

`protocol_view<I>` provides non-owning reference semantics. It is constructible
from any structurally conforming type, including `protocol<I>` itself. Method
calls are forwarded through a synthesised vtable.

Const-correctness is strictly maintained. A `const`-qualified `protocol` object,
or a `protocol_view<const I>`, permits the invocation of only `const`-qualified
member functions of the underlying erased object.

The owning `protocol` is fully allocator-aware and properly supports
`std::allocator_traits`.

The implementation of both types is generated automatically by the compiler
using reflection, eliminating the need for manually authored boilerplate.

### Design decisions

**Interface specification as a plain struct.** We chose to specify interfaces as
unannotated structs rather than introducing a new declaration syntax. This
decision favours minimal language impact and allows existing structs to serve as
interface definitions without modification.

**Valueless-after-move state for `protocol`.** We require that a moved-from
`protocol` enters a valueless state rather than retaining its previous value.
This is consistent with `std::variant` and avoids the cost of constructing a
sentinel object on move. Calling interface methods on a valueless `protocol`
object is undefined behaviour.

**`protocol_view` does not own.** We considered whether `protocol_view` should
support owning a small buffer optimisation. We rejected this in favour of a
strict non-owning contract, consistent with `std::string_view` and `std::span`.
Users requiring ownership should use `protocol`.

**Allocator awareness.** We require `protocol` to support `std::allocator_traits`
for consistency with other owning standard library containers. `protocol_view`
requires no allocation and is therefore not allocator-aware.

## Impact on the Standard

This is a library proposal that fundamentally depends on core-language reflection
facilities capable of programmatic class generation.

C++26 reflection (P2996) provides the introspection capabilities required to
validate structural conformity at compile time. However, synthesising a protocol
wrapper requires the ability to inject member functions into a class
definition—a generative capability not present in C++26. This proposal therefore
serves as a motivating use case for future extensions to the language's
reflection and code-injection facilities.

We anticipate that a complete implementation will be possible once member
function injection, as discussed in P3996 and related papers, is standardised.

## Technical Specifications

The synthesis of `protocol` and `protocol_view` relies on static reflection.

### Elements implementable with C++26 reflection

C++26 reflection (P2996) provides sufficient introspection for structural
validation. Using the reflection operator (`^`) and the `std::meta` namespace, a
metaprogram can extract the names, signatures, and `const`-qualifiers of the
member functions declared in an interface struct. It can then verify that a
candidate type provides matching member functions, replacing manually authored
concepts with a generic, reflection-driven structural constraint.

### Elements requiring post-C++26 additions

C++26 reflection does not yet support generalised code injection. The following
elements of the implementation require future standard additions.

The dispatch mechanism—whether a virtual hierarchy or a synthesised vtable—
requires injection of virtual functions or function pointers derived from an
introspected interface. `std::meta::define_class` is currently restricted to
non-static data members and does not support member function injection.

Generating the forwarding member functions of the `protocol` wrapper likewise
requires member function injection. Expanding introspected function parameters
into parameter packs for forwarding requires range-splicing of function arguments
and return types, which depends on more advanced injection proposals beyond P2996.

Until such injection facilities are standardised, a practical implementation
requires external tooling for code generation, with C++26 reflection handling
structural validation.

## Reference Implementation

A reference implementation using an AST-based Python code generator
(`py_cppmodel`) to simulate post-C++26 code injection is available at
[py_cppmodel]. The implementation demonstrates the feasibility of vtable
generation, allocator awareness, and the value semantics properties required by
this proposal.

## Acknowledgements

## References

[PEP 544] _Protocols: Structural subtyping (static duck typing)_.
<https://peps.python.org/pep-0544/>

[P3019] _std::indirect and std::polymorphic_.
<https://isocpp.org/files/papers/P3019R14.pdf>

[P2996] _Reflection for C++26_. <https://isocpp.org/files/papers/P2996R13.html>

[py_cppmodel] _Python wrappers for clang's parsing of C++ to simplify AST
analysis_. <https://github.com/jbcoe/py_cppmodel>

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

We propose a standard library facility for structural subtyping in C++ using
type erasure, value semantics, and static reflection. Building upon the
precedent set by PEP 544 in Python and P3019 (`std::polymorphic`), it defines a
mechanism by which interfaces can be satisfied without explicit inheritance. As
proposed, `protocol` decouples interface from implementation while maintaining
value semantics and providing support for custom allocators.

## History

### Changes in revision R0

- Initial revision.

## Motivation

Traditional polymorphic interfaces in C++ require inheritance from a common base
class. This tightly couples components, prevents the retroactive application of
interfaces to existing types, and inherently demands reference semantics, which
complicates memory management and reasoning about program state.

While `std::function` provides a mechanism for structural subtyping of single
invokable objects, no standardized equivalent currently exists for interfaces
comprising multiple member functions. Developers must resort to writing bespoke
type-erased wrappers to achieve open-set polymorphism without inheritance. This
practice demands substantial boilerplate for dispatch mechanisms and storage
management.

The concept of structural subtyping is well-established in other languages; for
instance, PEP 544 introduced Protocols in Python. Under this model, a class that
structurally implements the methods of a Protocol is considered a subtype
without requiring explicit inheritance.

This paper proposes a similar model for C++, leveraging reflection to synthesize
type-erased protocol wrappers from declarative interface definitions. Crucially,
these protocols maintain value semantics, enforce const-propagation, and provide
full allocator awareness, consistent with the design principles of P3019
(`std::polymorphic`).

Furthermore, while nominal subtyping naturally allows for non-owning polymorphic
views via simple raw pointers or references (e.g., `Base*` or `Base&`),
structural subtyping lacks a native language equivalent. To pass an object to a
function expecting a structural interface without transferring ownership or
triggering an allocation (a deep copy), a non-owning type-erased wrapper is
required. We propose `protocol_view` to fill this role. `protocol_view` acts
as a lightweight, zero-overhead reference to any structurally conforming type,
analogous to `std::string_view` or `std::span`.

## Examples

The following examples demonstrate the use of `protocol` for ownership and
`protocol_view` for non-owning observation using a conceptual `Drawable`
interface. Note that `Shape` does not inherit from `Drawable`; it satisfies the
interface structurally.

```cpp
struct Drawable {
  std::string_view name() const;
  void draw();
};

struct Circle {
  std::string_view name() const { return "Circle"; }
  void draw() { ++draw_count; }
  int draw_count = 0;
};
```

### `protocol` and value semantics

`xyz::protocol<I>` owns its contained object and provides value semantics.
Copying a `protocol` object performs a deep copy of the underlying type.

```cpp
void use_protocol() {
  // Construct in-place
  xyz::protocol<Drawable> p1(std::in_place_type<Circle>);

  // p2 is a deep copy of p1, including the underlying Circle object
  xyz::protocol<Drawable> p2 = p1;

  p1.draw();
  p1.draw();

  assert(p1.draw_count() == 2); // Assuming a draw_count() accessor
  assert(p2.draw_count() == 0); // p2 remains unaffected by changes to p1
}
```

### `protocol_view` and reference semantics

`xyz::protocol_view<I>` is a non-owning view of any type that satisfies the
interface `I`. It is analogous to `std::string_view`.

```cpp
void print_info(xyz::protocol_view<const Drawable> view) {
  // const view only allows calling const member functions
  std::cout << "Name: " << view.name() << "\n";
}

void do_work(xyz::protocol_view<Drawable> view) {
  // mutable view allows calling non-const member functions
  view.draw();
}

void use_view() {
  Circle circle;

  // View a concrete object directly without allocation or ownership transfer
  print_info(circle);
  do_work(circle);
  assert(circle.draw_count == 1);

  xyz::protocol<Drawable> p(std::in_place_type<Circle>);

  // View an owning protocol object
  print_info(p);
  do_work(p);

  // Copying a view is a shallow operation
  xyz::protocol_view<Drawable> v1(circle);
  xyz::protocol_view<Drawable> v2 = v1; // v2 points to the same 'circle' as v1
  v2.draw();
  assert(circle.draw_count == 2);
}
```

## Design requirements

The proposed protocol facility is guided by several core design requirements. It
must allow types to satisfy an interface based solely on the presence of
conforming member functions and signatures, without requiring explicit
inheritance. Protocols must provide value semantics, where copying a protocol
object performs a deep copy of the underlying erased type.

To support efficient observation at function boundaries without allocation or
ownership transfer, the facility must generate a non-owning `protocol_view`.
This view must be constructible from any structurally conforming type (including
the owning `protocol` itself) and route method calls through a synthesized
vtable.

Const correctness must be strictly maintained; a const-qualified protocol object
or a `protocol_view<const I>` must only permit the invocation of const-qualified
member functions on the underlying erased object. The implementation of the
type-erased wrappers should be generated automatically by the compiler using
reflection, eliminating the need for manual boilerplate. The owning protocol
must be fully allocator-aware, properly supporting `std::allocator_traits`.
Finally, to support efficient move operations without necessarily allocating
memory, the owning protocol must define a valueless-after-move state.

## Impact on the standard library

This is a library proposal that fundamentally relies on, and would require
additions to, core-language reflection facilities to allow classes with member
functions to be programmatically generated.

While C++26 reflection (as proposed in P2996) provides the necessary
introspection capabilities to validate structural conformity, the synthesis of a
protocol wrapper requires generative capabilities. Specifically, it requires the
ability to programmatically inject member functions into a class definition—a
feature that is not yet part of the standard or the current P2996 draft. This
proposal therefore serves as a motivating use case for, and depends on, future
extensions to the language's reflection and code-injection facilities.

## Technical specifications

The synthesis of protocol relies heavily on static reflection. While C++26
introduces foundational reflection capabilities (P2996), implementing this
proposal in its entirety requires features anticipated for future standards.

### Elements implementable with C++26 reflection

C++26 reflection provides sufficient introspection for structural validation.
Using the reflection operator (`^`) and functions provided by the `std::meta`
namespace, an interface struct can be inspected to extract the names,
signatures, and const-qualifiers of its required member functions. A metaprogram
can then verify if a concrete type structurally conforms to this interface. By
iterating over the introspected members, the compiler can check if the concrete
type provides matching member functions. This allows for the replacement of
manually authored concepts with a generic, reflection-based structural
constraint.

### Elements requiring post-C++26 additions

C++26 reflection currently lacks generalized code injection. Several critical
elements of the protocol implementation require future additions to the
standard.

The protocol implementation relies on a virtual dispatch hierarchy or a
synthesized vtable. Currently, `std::meta::define_class` is restricted to
generating non-static data members. Injecting the virtual functions or function
pointers that mirror an introspected interface requires generalized member
function injection.

Generating the public-facing member functions of the protocol wrapper that
forward calls to the erased type also requires member function injection.
Furthermore, generating the forwarding logic requires expanding introspected
function parameters into parameter packs. While C++26 supports basic splicing,
complex code injection involving the range splicing of function arguments and
return types relies on more advanced injection proposals.

Until such code injection facilities are standardized, the practical
implementation of this protocol mechanism requires external tooling, while C++26
reflection handles the structural validation.

## Reference implementation

A reference implementation using an AST-based Python code generator
(`py_cppmodel`) to simulate post-C++26 code injection is available. It
demonstrates the feasibility of the vtable generation, allocator awareness, and
value semantics properties required by this proposal.

## Acknowledgements

## References

[PEP 544] _Protocols: Structural subtyping (static duck typing)_.
<https://peps.python.org/pep-0544/>

[P3019] _std::indirect and std::polymorphic_.
<https://isocpp.org/files/papers/P3019R14.pdf>

[P2996] _Reflection for C++26_. <https://isocpp.org/files/papers/P2996R13.html>

[py_cppmodel] _Python wrappers for clang's parsing of C++ to simplify AST
analysis_. <https://github.com/jbcoe/py_cppmodel>

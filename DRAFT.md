# `protocol`: Structural Subtyping for C++ with Reflection

ISO/IEC JTC1 SC22 WG21 Programming Language C++

DXXXX-R0

Working Group: Library Evolution, Library

Date: 2026-03-23

_Jonathan Coe \<<jonathanbcoe@gmail.com>\>_


## Abstract

This paper proposes structural subtyping in C++ using type erasure, value semantics, and reflection. Building upon PEP 544 and P3019, it defines interfaces satisfied without explicit inheritance. Protocols decouple interface and implementation while maintaining value semantics and allocator awareness.

## History

### Changes in revision R0

* Initial revision.

## Motivation

C++ polymorphic interfaces require inheritance from a common base class. This couples components, prevents retroactive interface application, and requires reference semantics.

`std::function` provides structural subtyping for single invokable objects, but no standardized equivalent exists for interfaces with multiple member functions. Developers often write type-erased wrappers to achieve polymorphism without inheritance, requiring boilerplate for dispatch and storage.

PEP 544 introduced Protocols for structural subtyping in Python. A class that structurally implements the methods of a Protocol is a subtype without explicit inheritance.

This paper proposes a similar model using reflection to synthesize protocols from interfaces. Protocols maintain value semantics, const-propagation, and allocator awareness, consistent with P3019.

## Design requirements

Protocols require structural subtyping satisfied by member functions and signatures without explicit inheritance.

Protocols must provide value semantics, where copying performs a deep copy. Const propagation must be maintained; a const protocol must only permit calling const member functions on the underlying object.

Protocol implementation should be generated using reflection.

Protocols must be allocator-aware, supporting `std::allocator_traits`.

To support efficient move operations without allocating memory, the protocol must define a valueless-after-move state.

## Impact on the standard library

## Technical specifications

Protocol synthesis relies on reflection. While C++26 introduces reflection capabilities (P2996), implementing the proposal requires features slated for future standards.

### Elements implementable with C++26 reflection

C++26 reflection provides introspection for structural validation.

Using the reflection operator and functions provided by `std::meta`, an interface struct can be inspected to extract the names and signatures of required member functions. A metaprogram can verify if a concrete type structurally conforms to an interface. By iterating over the introspected members, the compiler checks if the concrete type provides matching member functions, replacing manually written concepts with a generic, reflection-based constraint.

### Elements requiring post-C++26 additions

C++26 reflection lacks generalized code injection. Some protocol elements require future additions.

Protocol relies on a virtual dispatch hierarchy. C++26's `std::meta::define_class` is restricted to generating non-static data members. Injecting virtual functions that mirror an introspected interface requires member function injection.

Synthesizing forwarding functions requires member function injection.

Generating forwarding logic requires expanding introspected function parameters into parameter packs. While C++26 supports basic splicing, complex code injection involving range splicing of function arguments and return types relies on advanced injection proposals.

Until code injection is standardized, protocol implementation requires external tooling, such as AST-based code generators, while C++26 handles structural validation.

## Reference implementation

## Acknowledgements

## References

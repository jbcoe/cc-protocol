# `protocol`: Structural Subtyping for C++

ISO/IEC JTC1 SC22 WG21 Programming Language C++

DXXXX-R0

Working Group: Library Evolution, Library

Date: 2026-03-24

_Jonathan Coe \<<jonathanbcoe@gmail.com>\>_

## Abstract

This paper proposes a standard library facility for structural subtyping in C++ using type erasure, value semantics, and static reflection. Building upon the precedent set by PEP 544 in Python and P3019 (`std::polymorphic`), it defines a mechanism by which interfaces can be satisfied without explicit inheritance. Protocols decouple interface from implementation while maintaining value semantics and allocator awareness.

## History

### Changes in revision R0

- Initial revision.

## Motivation

Traditional polymorphic interfaces in C++ require inheritance from a common base class. This tightly couples components, prevents the retroactive application of interfaces to existing types, and inherently demands reference semantics, which complicates memory management and reasoning about program state.

While `std::function` provides a mechanism for structural subtyping of single invokable objects, no standardized equivalent currently exists for interfaces comprising multiple member functions. Developers must resort to writing bespoke type-erased wrappers to achieve open-set polymorphism without inheritance. This practice demands substantial boilerplate for dispatch mechanisms and storage management.

The concept of structural subtyping is well-established in other languages; for instance, PEP 544 introduced Protocols in Python. Under this model, a class that structurally implements the methods of a Protocol is considered a subtype without requiring explicit inheritance.

This paper proposes a similar model for C++, leveraging reflection to synthesize type-erased protocol wrappers from declarative interface definitions. Crucially, these protocols maintain value semantics, enforce const-propagation, and provide full allocator awareness, consistent with the design principles of P3019 (`std::polymorphic`).

## Design requirements

The proposed protocol facility is guided by several core design requirements. It must allow types to satisfy an interface based solely on the presence of conforming member functions and signatures, without requiring explicit inheritance. Protocols must provide value semantics, where copying a protocol object performs a deep copy of the underlying erased type.

Const correctness must be strictly maintained; a const-qualified protocol object must only permit the invocation of const-qualified member functions on the underlying erased object. The implementation of the type-erased wrapper should be generated automatically by the compiler using reflection, eliminating the need for manual boilerplate. The wrapper must be fully allocator-aware, properly supporting `std::allocator_traits`. Finally, to support efficient move operations without necessarily allocating memory, the protocol must define a valueless-after-move state.

## Impact on the standard library

This is a library proposal that fundamentally relies on, and would require additions to, core-language reflection facilities to allow classes with member functions to be programmatically generated.

While C++26 reflection (as proposed in P2996) provides the necessary introspection capabilities to validate structural conformity, the synthesis of a protocol wrapper requires generative capabilities. Specifically, it requires the ability to programmatically inject member functions into a class definition—a feature that is not yet part of the standard or the current P2996 draft. This proposal therefore serves as a motivating use case for, and depends on, future extensions to the language's reflection and code-injection facilities.

## Technical specifications

The synthesis of protocol relies heavily on static reflection. While C++26 introduces foundational reflection capabilities (P2996), implementing this proposal in its entirety requires features anticipated for future standards.

### Elements implementable with C++26 reflection

C++26 reflection provides sufficient introspection for structural validation. Using the reflection operator (`^`) and functions provided by the `std::meta` namespace, an interface struct can be inspected to extract the names, signatures, and const-qualifiers of its required member functions. A metaprogram can then verify if a concrete type structurally conforms to this interface. By iterating over the introspected members, the compiler can check if the concrete type provides matching member functions. This allows for the replacement of manually authored concepts with a generic, reflection-based structural constraint.

### Elements requiring post-C++26 additions

C++26 reflection currently lacks generalized code injection. Several critical elements of the protocol implementation require future additions to the standard.

The protocol implementation relies on a virtual dispatch hierarchy or a synthesized vtable. Currently, `std::meta::define_class` is restricted to generating non-static data members. Injecting the virtual functions or function pointers that mirror an introspected interface requires generalized member function injection.

Generating the public-facing member functions of the protocol wrapper that forward calls to the erased type also requires member function injection. Furthermore, generating the forwarding logic requires expanding introspected function parameters into parameter packs. While C++26 supports basic splicing, complex code injection involving the range splicing of function arguments and return types relies on more advanced injection proposals.

Until such code injection facilities are standardized, the practical implementation of this protocol mechanism requires external tooling, while C++26 reflection handles the structural validation.

## Reference implementation

A reference implementation using an AST-based Python code generator (`py_cppmodel`) to simulate post-C++26 code injection is available. It demonstrates the feasibility of the vtable generation, allocator awareness, and value semantics properties required by this proposal.

## Acknowledgements

## References

[PEP 544] _Protocols: Structural subtyping (static duck typing)_.
<https://peps.python.org/pep-0544/>

[P3019] _std::indirect and std::polymorphic_.
<https://isocpp.org/files/papers/P3019R14.html>

[P2996] _Reflection for C++26_.
<https://isocpp.org/files/papers/P2996R13.html>

[py_cppmodel] _Python wrappers for clang's parsing of C++ to simplify AST analysis_.
<https://github.com/jbcoe/py_cppmodel>

# `protocol`: Structural Subtyping for C++

ISO/IEC JTC1 SC22 WG21 Programming Language C++

P4148R3

Working Group: Library Evolution, Library

Date: xxxx-xx-xx

_Jonathan Coe \<<jonathanbcoe@gmail.com>\>_

_Hana Dusikova \<<hanicka@hanicka.net>\>_

_Antony Peacock \<<ant.peacock@gmail.com>\>_

_Philip Craig \<<philip@pobox.com>\>_

_Neelofer Banglawala \<<dr.nbanglawala@gmail.com>\>_

## Abstract

We propose `protocol<T, A>` and `protocol_view<T>`, standard library vocabulary
types for structural subtyping in C++. Interfaces are specified as plain structs;
any type whose member functions satisfy the interface is accepted without
requiring explicit inheritance.

Any type that provides member functions with the same names and function
signatures as those specified by the interface is considered to be _conforming_
to the protocol.

The owning type, `protocol`, provides value semantics (const-propagation and
deep-copies) and support for custom allocators for any conforming type.

The non-owning type, `protocol_view`, provides a lightweight reference to any
conforming type, analogous to `std::span`.

Both `protocol` and `protocol_view` are implemented as ordinary class
templates using C++26 static reflection, eliminating hand-written type-erasure
boilerplate and custom build steps. No language features beyond C++26 are
required. This proposal focuses on the design of the class templates
`protocol` and `protocol_view`; Appendix A outlines the implementation.

## History

### Changes in revision R3

- Replace the code-injection sketch in Appendix A with the C++26 reflection design used by the reference implementation.
- Drop the requirement for post-C++26 code injection; C++26 reflection is sufficient.

### Changes in revision R2

- Support zero-cost conversion from a compatible `protocol` or `protocol_view` to a narrower target interface (subtype substitution).
- Add `any` to the standard library types equivalence table.

### Changes in revision R1

- Clarify special member function generation for `protocol` and `protocol_view`.
- Refine comparison with `std::proxy` (P3086).
- Add design alternatives section (relaxed structural subtyping, concepts, comparison operators).
- Add poll on reflection-based library features.

### Changes in revision R0

- Initial revision.

## Foreword

This is a very early stage design which we are sharing to further discussion
of design differences with a series of competing proposals for structural-subtyping.

This paper explores a different approach to proxy and relies on reflection rather than
templates for a smaller API surface.

## Motivation

C++ is a multi-paradigm language, supporting object-oriented, generic, and
functional programming styles. A key strength of the language is its ability
to express different forms of polymorphism, allowing developers to select the
most appropriate abstraction for a given context. However, this support is
uneven: while some paradigms are directly supported by the language, others
rely on idioms and library techniques.

One such case is dynamic structural polymorphism. While C++ provides strong
support for static structural typing through concepts, it lacks a corresponding
mechanism for runtime abstractions. In practice, this gap is addressed through
the widespread use of type-erasure.

Standard library facilities such as `std::function`, `std::any`,
`std::ranges::any_view` and the many other type-erasure based solutions demonstrate
that the need for dynamic structural interfaces is both real and recurring. However,
these solutions are implemented in an ad-hoc manner, requiring significant boilerplate
and leading to inconsistent semantics across libraries.

This situation can be understood in terms of the broader polymorphism design space:

|                   |  Static   | Dynamic |
| ----------------- | :-------: | :-----: |
| Nominal typing    | Templates | Virtual |
| Structural typing | Concepts  |   ---   |

The absence of a language-supported mechanism for dynamic structural typing explains
the proliferation of type-erasure-based abstractions. Each such abstraction can be
viewed as a manual encoding of a structural interface, tailored to a specific use case.

This paper proposes protocol types as a first-class library feature that fills this gap.
Protocols unify and generalise existing type-erasure patterns, providing a consistent,
non-intrusive mechanism for expressing dynamic structural polymorphism, while also
providing consistent support for allocators:

|                   |  Static   | Dynamic  |
| ----------------- | :-------: | :------: |
| Nominal typing    | Templates | Virtual  |
| Structural typing | Concepts  | Protocol |

## Design

In C++26 we introduced `polymorphic<T>` which confers value-like semantics on a
dynamically-allocated object. A `polymorphic<T>` may hold an object of a class
publicly derived from `T`. In this proposal, we seek to further extend C++'s
library of value-types with `protocol<T>` which can hold an object of any type
so long as that type is a structural sub-type of `T`.

Like `polymorphic`, `protocol` supports deep-copies, const propagation and
custom allocators. Like `polymorphic`, `protocol` has a valueless state after
being moved from to allow move construction and move assignment without
dynamic memory allocation.

Where `polymorphic<T>` is owning, `T*`, or `const T*` can be used as a
non-owning reference type. There is no base class to take a pointer to for
`protocol<T>` so we propose the addition of `protocol_view<T>` (and
`protocol_view<const T>`) which are similar to `span` and `string_view` and give
reference semantics to structural sub-types.

### Generated structural subtyping

For a given struct, the corresponding `protocol` and `protocol_view` will
implement all the public non-virtual, non-template member functions with
identical constexpr, noexcept and const-qualification.

Unlike `polymorphic`, `protocol` and `protocol_view` do not provide `operator*`
or `operator->` (or const-overloads) as there is no common base type to form a
pointer or reference. Member functions from a `protocol` or `protocol_view` are
generated so that the `protocol` or `protocol_view` is a valid structural subtype
and can be called with traditional `instance.member_function(args)` syntax.

```c++
struct I {
    std::string func0(std::string_view) const noexcept;
    double func1(double);
    int func2(int);
    int func2(int, int); // Another overload, same name.
};
```

We then generate a partial template specialization for `protocol` and
template specialization for `protocol_view`.

If the interface type `I` has deleted special member functions, then the
corresponding special member functions for `protocol` will not be generated.
For `protocol_view`, the copy constructor, move constructor, copy assignment and
move assignment (and allocator-extended equivalents) are generated unconditionally.

```c++
template <typename Allocator>
class protocol<I, Allocator=std::allocator<void>> {
  public:

    // Default constructor.
    explicit constexpr protocol(); // conditionally-generated

    // Constructor from any conforming value.
    template <class U>
    constexpr explicit protocol(U&& u);

    // In-place constructor.
    template <class U, class... Ts>
    explicit constexpr protocol(std::in_place_type_t<U>, Ts&&... ts);

    // In-place constructor with initializer_list.
    template <class U, class J, class... Ts>
    explicit constexpr protocol(std::in_place_type_t<U>,
                                std::initializer_list<J> ilist, Ts&&... ts);

    // Copy constructor.
    constexpr protocol(const protocol& other);  // conditionally-generated

    // Move constructor.
    constexpr protocol(protocol&& other) noexcept;  // conditionally-generated

    // Allocator-extended default constructor.
    explicit constexpr protocol(std::allocator_arg_t,
                                const Allocator& alloc);  // conditionally-generated

    // Allocator-extended constructor from any conforming value.
    template <class U>
    constexpr explicit protocol(std::allocator_arg_t, const Allocator& alloc, U&& u);

    // Allocator-extended in-place constructor.
    template <class U, class... Ts>
    explicit constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                                std::in_place_type_t<U>, Ts&&... ts);

    // Allocator-extended in-place constructor with initializer_list.
    template <class U, class J, class... Ts>
    explicit constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                                std::in_place_type_t<U>,
                                std::initializer_list<J> ilist, Ts&&... ts);

    // Allocator-extended copy constructor.
    constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                       const protocol& other);  // conditionally-generated

    // Allocator-extended move constructor.
    constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                       protocol&& other) noexcept;  // conditionally-generated

    // Narrowing copy constructor from any compatible protocol.
    template <typename Other>
    constexpr protocol(const protocol<Other, Allocator>& other);

    // Narrowing move constructor from any compatible protocol.
    template <typename Other>
    constexpr protocol(protocol<Other, Allocator>&& other) noexcept;


    // Allocator-extended narrowing copy constructor from any compatible protocol.
    template <typename Other>
    constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                       const protocol<Other, Allocator>& other);

    // Allocator-extended narrowing move constructor from any compatible protocol.
    template <typename Other>
    constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                       protocol<Other, Allocator>&& other) noexcept;

    // Destructor.
    ~protocol();

    // structural-subtype (const and non-const) member functions.
    std::string func0(std::string_view) const noexcept;
    double func1(double) const;
    int func2(int);
    int func2(int, int); // Another overload, same name.

    // valueless after move
    constexpr bool valueless_after_move() const noexcept;
};
```

```c++
template <>
class protocol_view<I> {
  public:

    // Constructor from any mutable conforming object.
    template <typename T>
    constexpr protocol_view(T& obj) noexcept;

    // Construction from an rvalue conforming object is deleted.
    template <typename T>
    protocol_view(const T&&) = delete;

    // Copy constructor
    constexpr protocol_view(const protocol_view&) noexcept = default;

    // Move constructor.
    constexpr protocol_view(protocol_view&&) noexcept = default;

    // Constructor from a mutable protocol.
    template <typename Alloc>
    protocol_view(protocol<I, Alloc>& p) noexcept;

    // Construction from a protocol rvalue is deleted.
    template <typename Alloc>
    protocol_view(protocol<I, Alloc>&&) = delete;

    // Narrowing constructor from any compatible mutable protocol_view.
    template <typename Other>
    protocol_view(const protocol_view<Other>& other) noexcept;

    // Narrowing constructor from any compatible mutable protocol.
    template <typename Other, typename Alloc>
    protocol_view(protocol<Other, Alloc>& p) noexcept;

    // Narrowing constructor from a compatible mutable protocol rvalue is deleted.
    template <typename Other, typename Alloc>
    protocol_view(protocol<Other, Alloc>&&) = delete;

    // structural-subtype (const and non-const) member functions.
    std::string func0(std::string_view) const noexcept;
    double func1(double) const;
    int func2(int);
    int func2(int, int); // Another overload, same name.
};
```

```c++
template <>
class protocol_view<const I> {
  public:

    // Constructor from any const conforming object.
    template <typename T>
    constexpr protocol_view(const T& obj) noexcept;

    // Construction from a const rvalue conforming object is deleted.
    template <typename T>
    protocol_view(const T&&) = delete;

    // Copy constructor
    constexpr protocol_view(const protocol_view&) noexcept = default;

    // Move constructor.
    constexpr protocol_view(protocol_view&&) noexcept = default;

    // Constructor from a const protocol.
    template <typename Alloc>
    protocol_view(const protocol<I, Alloc>& p) noexcept;

    // Construction from a const protocol rvalue is deleted.
    template <typename Alloc>
    protocol_view(const protocol<I, Alloc>&&) = delete;

    // Constructor from a mutable protocol.
    template <typename Alloc>
    protocol_view(protocol<I, Alloc>& p) noexcept;

    // Construction from a protocol rvalue is deleted.
    template <typename Alloc>
    protocol_view(protocol<I, Alloc>&&) = delete;

    // Constructor from a mutable protocol_view<I>.
    constexpr protocol_view(protocol_view<I> view) noexcept;

    // Narrowing constructor from any compatible const protocol_view.
    template <typename Other>
    protocol_view(const protocol_view<const Other>& other) noexcept;

    // Narrowing constructor from any compatible mutable protocol_view.
    template <typename Other>
    protocol_view(const protocol_view<Other>& other) noexcept;

    // Narrowing constructor from any compatible const protocol.
    template <typename Other, typename Alloc>
    protocol_view(const protocol<Other, Alloc>& p) noexcept;

    // Narrowing constructor from a compatible const protocol rvalue is deleted.
    template <typename Other, typename Alloc>
    protocol_view(const protocol<Other, Alloc>&&) = delete;

    // Narrowing constructor from any compatible mutable protocol.
    template <typename Other, typename Alloc>
    protocol_view(protocol<Other, Alloc>& p) noexcept;

    // Narrowing constructor from a compatible mutable protocol rvalue is deleted.
    template <typename Other, typename Alloc>
    protocol_view(protocol<Other, Alloc>&&) = delete;

    // structural-subtype const member functions.
    std::string func0(std::string_view) const noexcept;
    double func1(double) const;
};
```

Narrowing construction is allowed when the target interface specifies a structural subset of the source interface's member functions with matching parameter types, return types, and compatible qualifiers (such as `noexcept`).

Code generation is currently implemented in a reference implementation with a
custom build step but would be better implemented with generative reflection post
C++26.

### Function-like examples

We can use `protocol` and `protocol_view` with appropriate
structural types to implement and extend the standard library's
existing set of function-objects.

Consider the structural types below:

```c++
struct Any {
  // All special member functions are defaulted.
};
```

```c++
struct MoveOnlyAny {
  // Deleted copy constructor and copy assignment.
  MoveOnlyAny(const MoveOnlyAny&) = delete;
  MoveOnlyAny& operator=(const MoveOnlyAny&) = delete;

  // Defaulted move constructor and move assignment.
  MoveOnlyAny(MoveOnlyAny&&) = default;
  MoveOnlyAny& operator=(MoveOnlyAny&&) = default;
};
```

```c++
template <typename R, typename... Args>
struct Function {
    // All special member functions are defaulted.
    R operator()(Args&&... args) const;
};
```

```c++
template <typename R, typename... Args>
struct MoveOnlyFunction {
    // Deleted copy constructor and copy assignment.
    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    // Defaulted move constructor and move assignment.
    MoveOnlyFunction(MoveOnlyFunction&&) = default;
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) = default;

    R operator()(Args&&... args) const;
};
```

```c++
template <typename R, typename... Args>
struct MutatingFunction {
    // All special member functions are defaulted.
    R operator()(Args&&... args);
};
```

```c++
template <typename R, typename... Args>
struct MoveOnlyMutatingFunction {
    // Deleted copy constructor and copy assignment.
    MoveOnlyMutatingFunction(const MoveOnlyMutatingFunction&) = delete;
    MoveOnlyMutatingFunction& operator=(const MoveOnlyMutatingFunction&) = delete;

    // Defaulted move constructor and move assignment.
    MoveOnlyMutatingFunction(MoveOnlyMutatingFunction&&) = default;
    MoveOnlyMutatingFunction& operator=(MoveOnlyMutatingFunction&&) = default;

    R operator()(Args&&... args);
};
```

```c++
struct OverloadedFunction {
    // All special member functions are defaulted.
    R1 operator()(Args1&&... args) const;
    R2 operator()(Args2&&... args);
    R3 operator()(Args3&&... args);
};
```

There is currently no function-type in the standard library that can represent an
overload set. The table below is illustrative of how flexible `protocol` and
`protocol_view` are:

| Standard library type                  | Protocol equivalent                              |
| :------------------------------------- | :----------------------------------------------- |
| `any`                                  | `protocol<Any>`                                  |
| ???                                    | `protocol<MoveOnlyAny>`                          |
| `copyable_function<R(Args...) const>`  | `protocol<Function<R, Args...>>`                 |
| `move_only_function<R(Args...) const>` | `protocol<MoveOnlyFunction<R, Args...>>`         |
| `function_ref<R(Args...) const>`       | `protocol_view<Function<R, Args...>>`            |
| `copyable_function<R(Args...)>`        | `protocol<MutatingFunction<R, Args...>>`         |
| `move_only_function<R(Args...)>`       | `protocol<MoveOnlyMutatingFunction<R, Args...>>` |
| `function_ref<R(Args...)>`             | `protocol_view<MutatingFunction<R, Args...>>`    |
| ???                                    | `protocol<OverloadedFunction>`                   |
| ???                                    | `protocol_view<OverloadedFunction>`              |

### Comparison with proxy

`proxy` (P3086, implemented in `ngcpp/proxy`) occupies an overlapping region of
the design space: both proposals provide type-erased, non-intrusive runtime
polymorphism without requiring inheritance.

#### Interface Definition

`protocol` defines an interface as a C++ struct
containing member-function declarations. The library (or compiler, given
reflection) introspects the struct to synthesise a vtable and forwarding member
functions. `proxy` requires the author to build a _Facade_ explicitly using the
`pro::facade_builder` template, combining dispatch objects such as
`pro::member_dispatch` with `add_convention` calls. The `protocol` approach is
unobtrusive: any existing struct, including those in third-party headers, can
serve as an interface without modification. The `proxy` approach gives the
author precise control over dispatch conventions but couples the interface
definition to library implementation details.

#### Interaction syntax

`protocol` synthesises member functions directly on
the wrapper, so callers can call member functions directly: `p.draw()`.
`proxy` requires indirection: `p->draw()`.
Using `operator->` avoids potential name collisions with the erased type's
methods; allowing direct member function calls makes a `protocol<T>` a
drop-in structural substitute for any type conforming to `T`.

#### Layout constraints

A `proxy` Facade encodes physical layout constraints directly in the type. This
enables the compiler to apply `memcpy`-based relocation and to enforce specific
memory budgets per interface. `protocol`, like `polymorphic` and `function`, does
not prescribe any layout constraints and leaves details like small-buffer-optimization
to be determined by implementers.

#### Ownership Erasure

`protocol` is uniquely owning, `protocol_view` is non-owning.
`proxy` can store any suitable pointer-like object and offers a
lifetime-independent interface where the lifetime of the pointer-like
object is determined by the choice of pointer, not by `proxy`.
`proxy_view` is, like `protocol_view`, non-owning.

#### Summary table

The table below summarises the main design choices side by side.

| Aspect               | `protocol`             | `proxy`                                        |
| :------------------- | :--------------------- | :--------------------------------------------- |
| Interface definition | C++ struct             | `facade_builder` + dispatch objects (explicit) |
| Interaction syntax   | `p.draw()`             | `p->draw()`                                    |
| Layout constraints   | Implementation defined | Encoded in the Facade type                     |
| Ownership model      | Explicit               | Erased                                         |

### Design Alternatives

We discuss design alternatives that were considered and why they were not adopted.

#### Relaxed structural subtyping

We require a type to have exactly matching function signatures as `I` to be considered a
conforming type for `protocol<I>`. Implicit conversions _could_ be allowed but this might
lead to odd chains of implicit-conversion-led conformance where an object can be passed
through a sequence of `protocol` (or `protocol_view`) objects to conform to the interface
of the last `protocol`. Where implicit conversions are unidirectional this may lead to
undesirable or surprising behaviour.

With some suitably compelling motivation, conformance via implicit conversions could be
added to `protocol` in a later revision of the C++ standard without rendering existing code
ill-formed.

#### Structure defined with concepts

We use a struct rather than a concept to define the interface of the `protocol<I>`
(and `protocol_view<I>`) specialization. A concept could be used but concepts are a more
expert feature than is necessary to define a structural subtyping interface.

Internally, our reference implementation defines a concept from the interface struct to
generate better compiler errors when non-conforming types are used.

#### Equality and comparison operators

We do not generate equality or comparison operators. If the interface struct `I` in
`protocol<I>` defines equality or comparison operators as inline friends or member functions,
these are not generated for `protocol` or `protocol_view`.

Equality or comparison operators are not part of the core functionality of `protocol` or
`protocol_view` but could be added in a later revision of the C++ standard.

## Impact on the Standard

This proposal is a library extension. It requires C++26 static reflection
and the addition of a new standard library header `<protocol>`. No further
language support is required.

## Polls

- Should we work to standardize `protocol` and `protocol_view`?

- Is implementing something _like_ `protocol` and `protocol_view`, their design
  details aside, something that we would like C++ reflection to be able to do?

## Reference Implementation

Two reference implementations are available at
<https://github.com/jbcoe/cc-protocol>. The first uses an AST-based Python code
generator and is feature-complete; it demonstrates vtable generation, allocator
awareness, narrowing conversions and the value semantics properties required by
this proposal. The second is written in C++26 using static reflection, builds
with GCC, and is described in Appendix A; it is in progress and does not yet
support overloaded member functions or operators.

## Acknowledgements

The authors would like to thank Billy Baker, Tony van Eerd and the BSI C++
Panel for suggestions and useful discussion.

## References

[PEP 544] _Protocols: Structural subtyping (static duck typing)_.
<https://peps.python.org/pep-0544/>

[P3019] _std::indirect and std::polymorphic_.
<https://isocpp.org/files/papers/P3019R14.pdf>

[P2996] _Reflection for C++26_. <https://isocpp.org/files/papers/P2996R13.html>

[Metaclasses for generative C++]
<https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p0707r5.pdf>

[P3086R4 Proxy]
<https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3086r5.html>

[py_cppmodel] _Python wrappers for clang's parsing of C++ to simplify AST
analysis_. <https://github.com/jbcoe/py_cppmodel>

## Appendix A: Implementation with C++26 Reflection

An earlier revision of this appendix sketched an implementation in terms of
hypothetical post-C++26 code-injection primitives. That sketch assumed a
reflection-based implementation would need to synthesise the same constructs a
source-level code generator emits: named concepts, class template
specialisations, virtual and override member functions and named forwarding
member functions. None of these are needed. The design below uses only
features of C++26 reflection ([P2996], expansion statements and consteval
blocks) and is the basis of the reference implementation, which builds and
passes tests with GCC.

The implementation has three parts, shown here for `protocol_view`;
`protocol` adds owning storage, allocator support and destroy/copy/move
entries to the vtable but generates its member functions in exactly the same
way.

### Conformance

Structural conformance is a consteval function that compares the named,
non-static member functions of the interface with those of a candidate type:
same identifier, same `const`, `noexcept` and reference qualification, and
same de-aliased return and parameter types. It is used directly in
`requires` clauses; no concept needs to be synthesised.

```cpp
namespace detail {

// The named, non-static member functions of `Type`.
template <std::meta::info Type>
constexpr inline auto interface_functions_of = std::define_static_array(
    members_of(Type, std::meta::access_context::unprivileged()) |
    std::views::filter(std::meta::is_function) |
    std::views::filter(std::not_fn(std::meta::is_static_member)) |
    std::views::filter(std::meta::has_identifier));

// Compares identifier, qualifiers and de-aliased signature.
consteval bool member_function_conforms_to(std::meta::info candidate,
                                           std::meta::info interface);

template <std::meta::info Member, std::meta::info CandidateType>
consteval std::meta::info find_conforming_member() {
  for (std::meta::info candidate : interface_functions_of<CandidateType>) {
    if (member_function_conforms_to(candidate, Member)) return candidate;
  }
  std::unreachable();
}

}  // namespace detail

template <typename Interface, typename Candidate>
consteval bool is_protocol_conformant() {
  return std::ranges::all_of(
      detail::interface_functions_of<^^Interface>,
      [](std::meta::info interface_member) {
        return std::ranges::any_of(
            detail::interface_functions_of<^^Candidate>,
            [&](std::meta::info candidate_member) {
              return detail::member_function_conforms_to(candidate_member,
                                                         interface_member);
            });
      });
}
```

### Vtable

The vtable is a struct of function pointers synthesised with
`define_aggregate`. Each entry is named after the interface member function it
dispatches and takes a type-erased object pointer (`const void*` for a
`const` member, `void*` otherwise) ahead of the member's own parameters. The
function pointer type is built with `substitute` from the reflected return
and parameter types.

Entries are populated by trampolines: class templates parameterised on the
concrete type `U` and on a reflection of the conforming member of `U`, which
recover `U` from the erased pointer and call the member through a splice.

```cpp
namespace detail {

template <bool Noexcept, typename R, typename... Args>
using fn_ptr_t = R (*)(Args...) noexcept(Noexcept);

template <std::meta::info Interface>
consteval std::vector<std::meta::info> vtable_specs() {
  std::vector<std::meta::info> specs;
  for (std::meta::info member : interface_functions_of<Interface>) {
    std::vector<std::meta::info> signature{
        std::meta::reflect_constant(is_noexcept(member)),
        dealias(return_type_of(member)),
        is_const(member) ? ^^const void* : ^^void*};
    for (std::meta::info parameter : parameters_of(member)) {
      signature.push_back(dealias(type_of(parameter)));
    }
    specs.push_back(data_member_spec(
        substitute(^^fn_ptr_t, signature),
        std::meta::data_member_options{.name = identifier_of(member)}));
  }
  return specs;
}

template <typename Interface>
struct vtable_generator {
  struct vtable;
  consteval { define_aggregate(^^vtable, vtable_specs<^^Interface>()); }
};

// The vtable entry with the same name as `Member`.
template <std::meta::info Vtable, std::meta::info Member>
consteval std::meta::info vtable_entry_for() {
  for (std::meta::info entry :
       members_of(Vtable, std::meta::access_context::unprivileged())) {
    if (has_identifier(entry) &&
        identifier_of(entry) == identifier_of(Member)) {
      return entry;
    }
  }
  std::unreachable();
}

template <typename FnPtr, typename U, std::meta::info Member>
struct trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info Member>
struct trampoline<R (*)(void*, Args...) noexcept(Noexcept), U, Member> {
  static R call(void* object, Args... args) noexcept(Noexcept) {
    return static_cast<U*>(object)->[:Member:](args...);
  }
};

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info Member>
struct trampoline<R (*)(const void*, Args...) noexcept(Noexcept), U, Member> {
  static R call(const void* object, Args... args) noexcept(Noexcept) {
    return static_cast<const U*>(object)->[:Member:](args...);
  }
};

template <typename Interface, typename U>
consteval typename vtable_generator<Interface>::vtable make_vtable() {
  using Vtable = typename vtable_generator<Interface>::vtable;
  Vtable result{};
  template for (constexpr std::meta::info member :
                interface_functions_of<^^Interface>) {
    constexpr std::meta::info entry = vtable_entry_for<^^Vtable, member>();
    constexpr std::meta::info candidate =
        find_conforming_member<member, ^^U>();
    result.[:entry:] =
        &trampoline<typename[:type_of(entry):], U, candidate>::call;
  }
  return result;
}

template <typename Interface, typename U>
inline constexpr typename vtable_generator<Interface>::vtable vtable_for =
    make_vtable<Interface, U>();

}  // namespace detail
```

### Member function call syntax

C++26 reflection cannot define member functions, but it can define data
members, and a data member of class type can have an `operator()`. For each
interface member function we synthesise, with `define_aggregate`, an empty
base class containing a single `[[no_unique_address]]` data member named after
the member function whose type is a thunk with an `operator()` of the member's
signature. `protocol_view` derives from all of these bases, so
`view.name(args)` resolves to `view.name.operator()(args)`.

The thunk must reach the vtable and object pointers stored in the enclosing
`protocol_view`. It is the first non-static data member of a standard-layout
class, so its `this` pointer is pointer-interconvertible with the base-class
subobject, and the base-to-derived conversion is an ordinary `static_cast`.
The thunk is given the enclosing wrapper type and vtable type as template
arguments so that the cast and the vtable entry lookup are both resolved at
compile time.

```cpp
namespace detail {

template <typename FnPtr, typename Base, typename Wrapper, typename Vtable,
          std::meta::info Member, bool IsConst>
struct thunk;

template <typename R, typename... Args, bool Noexcept, typename Base,
          typename Wrapper, typename Vtable, std::meta::info Member,
          bool IsConst>
struct thunk<R (*)(Args...) noexcept(Noexcept), Base, Wrapper, Vtable, Member,
             IsConst> {
  R operator()(Args... args) noexcept(Noexcept)
    requires(!IsConst)
  {
    auto* wrapper = static_cast<Wrapper*>(reinterpret_cast<Base*>(this));
    constexpr std::meta::info entry = vtable_entry_for<^^Vtable, Member>();
    return (*wrapper->vtable_).[:entry:](wrapper->object_, args...);
  }

  R operator()(Args... args) const noexcept(Noexcept)
    requires(IsConst)
  {
    const auto* wrapper =
        static_cast<const Wrapper*>(reinterpret_cast<const Base*>(this));
    constexpr std::meta::info entry = vtable_entry_for<^^Vtable, Member>();
    return (*wrapper->vtable_).[:entry:](wrapper->object_, args...);
  }
};

template <std::meta::info Member, typename Wrapper, typename Vtable>
struct member_base_generator {
  struct member_base;
  consteval {
    std::vector<std::meta::info> signature{
        std::meta::reflect_constant(is_noexcept(Member)),
        dealias(return_type_of(Member))};
    for (std::meta::info parameter : parameters_of(Member)) {
      signature.push_back(dealias(type_of(parameter)));
    }
    std::meta::info thunk_type = substitute(
        ^^thunk, {substitute(^^fn_ptr_t, signature), ^^member_base, ^^Wrapper,
                  ^^Vtable, std::meta::reflect_constant(Member),
                  std::meta::reflect_constant(is_const(Member))});
    define_aggregate(
        ^^member_base,
        {data_member_spec(thunk_type, std::meta::data_member_options{
                                          .name = identifier_of(Member),
                                          .no_unique_address = true})});
  }
};

template <std::meta::info Member, typename Wrapper, typename Vtable>
using member_base_t =
    member_base_generator<Member, Wrapper, Vtable>::member_base;

template <typename... Bases>
struct wrapper_bases : Bases... {};

template <typename Interface, typename Wrapper, typename Vtable>
consteval std::meta::info generate_wrapper_bases() {
  std::vector<std::meta::info> bases;
  for (std::meta::info member : interface_functions_of<^^Interface>) {
    bases.push_back(dealias(substitute(
        ^^member_base_t, {reflect_constant(member), ^^Wrapper, ^^Vtable})));
  }
  return substitute(^^wrapper_bases, bases);
}

template <typename Interface, typename Wrapper, typename Vtable>
using wrapper_bases_t =
    typename[:generate_wrapper_bases<Interface, Wrapper, Vtable>():];

}  // namespace detail

template <typename Interface>
class protocol_view
    : public detail::wrapper_bases_t<
          Interface, protocol_view<Interface>,
          typename detail::vtable_generator<Interface>::vtable> {
  using vtable = typename detail::vtable_generator<Interface>::vtable;

  template <typename, typename, typename, typename, std::meta::info, bool>
  friend struct detail::thunk;

  void* object_;
  const vtable* vtable_;

 public:
  template <typename U>
    requires(is_protocol_conformant<Interface, U>() && !std::is_const_v<U>)
  explicit protocol_view(U& object)
      : object_(std::addressof(object)),
        vtable_(&detail::vtable_for<Interface, U>) {}
};
```

With this, the following compiles and dispatches through the generated vtable:

```cpp
struct Shape {
  double area() const;
  void scale(double factor);
};

struct Square {
  double side;
  double area() const { return side * side; }
  void scale(double factor) { side *= factor; }
};

Square square{2.0};
protocol_view<Shape> view(square);
view.scale(3.0);
assert(view.area() == 36.0);
```

### Remaining work

The following are unfinished in the reference implementation but require no
reflection features beyond C++26:

- Overloaded member functions. Two bases exposing the same member name make
  lookup through the derived class ambiguous, and `define_aggregate` rejects
  two data members with the same name. One approach is a single base per
  member name whose thunk carries all of that name's `operator()` overloads,
  with vtable entries named by index rather than by identifier.
- Operators and reference-qualified member functions, which need the thunk's
  `operator()` to carry the corresponding qualification.
- Narrowing conversions between views of different interfaces.

Post-C++26 code injection, if adopted, would allow the forwarding member
functions to be written directly rather than through the thunk bases above.
That is an ergonomic improvement, not a prerequisite.

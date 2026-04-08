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
refer non-owningly to any conforming type, enabling efficient observation at
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

This proposal is a pure library extension. It requires a new standard library header `<protocol>`.

The proposal fundamentally depends on core-language reflection facilities capable of programmatic class 
generation. C++26 reflection (P2996) provides the introspection capabilities required to validate structural
conformity at compile time. However, synthesising a protocol wrapper requires the ability to inject member
functions into a class definition—a generative capability not present in C++26. This proposal therefore serves
as a motivating use case for future extensions to the language's reflection and code-injection facilities.

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

## Technical Wording

## Header `<version>` synopsis [version.syn]

Note to editors: Add the following macro with editor provided values to
[version.syn]

```cpp
#define __cpp_lib_protocol ??????L
```

### Header `<protocol>` synopsis [protocol.syn]

```cpp
namespace std {

  // [protocol], class template protocol
  template<class I, class Allocator = allocator<I>>
    class protocol;

  // [protocol_view], class template protocol_view
  template<class I>
    class protocol_view;

  namespace pmr {
    template<class I> using protocol =
      protocol<I, polymorphic_allocator<I>>;

    template<class I> using protocol_view =
      protocol_view<I>;
  }
}
```

### X.Y Class template protocol [protocol]

[Drafting note: The member _`alloc`_ and _`vtable`_ should be formatted as exposition only identifiers,
but limitations of the processor used to prepare this paper means not all uses are italicised.]

#### X.Y.1 Class template protocol general [protocol.general]

1. A `protocol` object manages the lifetime of an owned object that erases the type of a conforming 
implementation. A `protocol` object is _valueless_ if it has no owned object. A `protocol` object may
become valueless only after it has been moved from.

2. An object of type `T` _conforms to an interface_ `I` if all member functions declared in `I` are
available on `T` with matching signatures and are not deleted. Const-qualifiers in the interface's
member functions are required to match.

3. In every specialization `protocol<I, Allocator>`, if the type `allocator_traits<Allocator>::value_type`
is not the same type as `I`, the program is ill-formed. Every object of type `protocol<I, Allocator>`
uses an object of type `Allocator` to allocate and free storage for the owned object as needed.

4. The member `alloc` is used for any memory allocation and element construction performed by member
functions during the lifetime of each `protocol` object. The allocator `alloc` may be replaced only by
assignment or `swap()`. Allocator replacement is performed by copy assignment, move assignment, or swapping
of the allocator only if ([container.reqmts]):
  `allocator_traits<Allocator>::propagate_on_container_copy_assignment::value`, or\
  `allocator_traits<Allocator>::propagate_on_container_move_assignment::value`, or\
  `allocator_traits<Allocator>::propagate_on_container_swap::value`
is `true` within the implementation of the corresponding `protocol` operation.

5. A program that instantiates the definition of the template `protocol<I, Allocator>` with a type for the
`I` parameter that is a non-object type, an array type, or a cv-qualified type is ill-formed.

6. The template parameter `I` of `protocol` shall be a complete type. The program is ill-formed if a
type instantiates `protocol<I, Allocator>` with an incomplete type `I`.

7. The template parameter `Allocator` of `protocol` shall meet the
_Cpp17Allocator_ requirements.

8. If a program declares an explicit or partial specialization of `protocol`, the behavior is undefined.

## Header `<version>` synopsis [version.syn]

Note to editors: Add the following macros with editor provided values to
[version.syn]

```cpp
#define __cpp_lib_protocol ??????L
```

#### X.Y.2 Class template protocol synopsis [protocol.syn]

```cpp
template <class I, class Allocator = allocator<I>>
class protocol {
 public:
  using interface_type = I;
  using allocator_type = Allocator;
  using pointer = typename allocator_traits<Allocator>::pointer;
  using const_pointer = typename allocator_traits<Allocator>::const_pointer;

  constexpr protocol(const protocol& other);

  constexpr protocol(allocator_arg_t, const Allocator& a,
                     const protocol& other);

  constexpr protocol(protocol&& other) noexcept;

  constexpr protocol(allocator_arg_t, const Allocator& a,
                     protocol&& other) noexcept(see below);

  template<class T>
  explicit constexpr protocol(in_place_type_t<T>);

  template<class T>
  explicit constexpr protocol(allocator_arg_t, const Allocator& a,
                              in_place_type_t<T>);

  template<class T, class... Us>
  explicit constexpr protocol(in_place_type_t<T>, Us&&... us);

  template<class T, class... Us>
  explicit constexpr protocol(allocator_arg_t, const Allocator& a,
                              in_place_type_t<T>, Us&&... us);

  ~protocol();

  constexpr protocol& operator=(const protocol& other);
  
  constexpr protocol& operator=(protocol&& other) noexcept(see below);

  constexpr allocator_type get_allocator() const;

  constexpr bool valueless_after_move() const noexcept;

  constexpr void swap(protocol& other) noexcept(see below);

  //constexpr void swap(protocol& lhs, protocol& rhs) noexcept(noexcept(lhs.swap(rhs)));

 private:
  //pointer p;                                   // exposition only
  Allocator alloc = Allocator();                 // exposition only
};
```

#### X.Y.3 Constructors [protocol.ctor]

The following element applies to all functions in [protocol.ctor]:

_Throws_: Nothing unless `allocator_traits<Allocator>::allocate` or
`allocator_traits<Allocator>::construct` throws.

```cpp
constexpr protocol(const protocol& other);
```

3. _Effects_: `alloc` is direct-non-list-initialized with\
`allocator_traits<Allocator>::select_on_container_copy_construction(other.alloc)`.
If `other` is valueless, `*this` is valueless. Otherwise, constructs an owned object
of the same type as the owned object in `other`, with the object owned in `other`
using the allocator `alloc`. 

```cpp
constexpr protocol(allocator_arg_t, const Allocator& a,
                   const protocol& other);
```

4. _Effects_: `alloc` is direct-non-list-initialized with `a`. If `other` is
valueless, `*this` is valueless. Otherwise, constructs an owned object of the same
type as the owned object in `other`, with the object owned in `other` using
the allocator `alloc`. 

```cpp
constexpr protocol(protocol&& other) noexcept;
```

5. _Effects_: `alloc` is direct-non-list-initialized from `std::move(other.alloc)`.
If `other` is valueless, `*this` is valueless. Otherwise `*this` takes
ownership of the owned object of `other`, or owns an object of the same type
constructed from the owned object of `other` considering that owned object
as an rvalue, using the allocator `alloc`.

```cpp
constexpr protocol(allocator_arg_t, const Allocator& a, protocol&& other)
  noexcept(allocator_traits<Allocator>::is_always_equal::value);
```

6. _Effects_: `alloc` is direct-non-list-initialized with `a`. If `other` is
valueless, `*this` is valueless. Otherwise, if `alloc == other.alloc` is `true`,
either constructs an object of type `protocol` that owns the owned object of other,
making `other` valueless; or, owns an object of the same type constructed from the
owned object of `other` considering that owned object as an rvalue. Otherwise, if
`alloc != other.alloc` is `true`, constructs an owned object of the same type
as the owned object in `other`, with the owned object in `other` as an rvalue,
using the allocator `alloc`.

```cpp
template<class T>
explicit constexpr protocol(in_place_type_t<T>);
```

7. _Constraints_:
  * `is_default_constructible_v<T>` is `true`, and
  * `T` conforms to interface `I`.

8. _Effects_: Constructs an owned object of type `T` with an empty argument list
using the allocator `alloc`.

```cpp
template<class T>
explicit constexpr protocol(allocator_arg_t, const Allocator& a,
                            in_place_type_t<T>);
```

9. _Constraints_: 
  * `is_default_constructible_v<T>` is `true`, and
  * `T` conforms to interface `I`.

10. _Effects_: `alloc` is direct-non-list-initialized with `a`. Constructs an 
owned object of type `T` with an empty argument list using the allocator `alloc`.

```cpp
template<class T, class... Us>
explicit constexpr protocol(in_place_type_t<T>, Us&&... us);
```

11. _Constraints_:
  * `is_same_v<remove_cvref_t<T>, T>` is `true`,
  * `is_constructible_v<T, Us...>` is `true`,
  * `T` conforms to interface `I`, and
  * `is_default_constructible_v<Allocator>` is `true`.

12. _Effects_: Constructs an owned object of type `T` with `std::forward<Us>(us)...`
using the allocator `alloc`.

```cpp
template<class T, class... Us>
explicit constexpr protocol(allocator_arg_t, const Allocator& a,
                            in_place_type_t<T>, Us&&... us);
```

13. _Constraints_:
  * `is_same_v<remove_cvref_t<T>, T>` is `true`,
  * `is_constructible_v<T, Us...>` is `true`, and
  * `T` conforms to interface `I`.

14. _Effects_: `alloc` is direct-non-list-initialized with `a`. Constructs an 
owned object of type `T` with `std::forward<Us>(us)...` using the allocator 
`alloc`.

#### X.Y.4 Destruction [protocol.dtor]

```cpp
~protocol();
```

1. _Effects_: If `*this` is not valueless, calls `allocator_traits<Allocator>::destroy(p)`, 
where `p` is a pointer of type `U*` to the owned object and `U` is the type of the owned object; 
then the storage is deallocated.


#### X.Y.5 Assignment [protocol.assign]

```cpp
constexpr protocol& operator=(const protocol& other);
```

1. _Mandates_: `T` is a complete type.

2. _Effects_: If `addressof(other) == this` is `true`, there are no effects. Otherwise:

Changes `*this` to a deep copy of the owned object of `other`. If 
`other` is valueless, `*this` is valueless after assignment. If an exception is
thrown, `*this` is unchanged. If `*this` already contains an owned object and 
`alloc == other.alloc`, the assignment is performed in-place where possible;
otherwise the old owned object is destroyed and a new one is created.

  2.1. The allocator needs updating if\
  `allocator_traits<Allocator>::propagate_on_container_copy_assignment::value`\
  is `true`.

  2.2. If `other` is not valueless, a new owned object of type `U`, where `U` is the type of
  the owned object in `other`, is constructed in `*this` using `allocator_traits<Allocator>::construct` 
  with the owned object from `other` as the argument, using either the allocator in `*this`
  or the allocator in `other` if the allocator needs updating.

  2.3 The previously owned object in `*this`, if any, is destroyed using `allocator_traits<Allocator>::destroy`
  and then the storage is deallocated.

  2.4 If the allocator needs updating, the allocator in `*this` is replaced with a copy of the allocator in
  `other`.

3. _Returns_: `*this`.

4. _Remarks_: If any exception is thrown, there are no effects on `*this`.

```cpp
protocol& operator=(protocol&& other) noexcept(
  allocator_traits<Allocator>::propagate_on_container_move_assignment::value  ||
  allocator_traits<Allocator>::is_always_equal::value);
```

5. _Mandates_: If `allocator_traits<Allocator>​::​propagate_on_container_move_assignment​::​value` is `false`
and `allocator_traits<Allocator>​::​is_always_equal​::​value` is `false`, `T` is a complete type.

6. _Effects_: If `addressof(other) == this` is `true`, there are no effects. Otherwise:

  6.1. The allocator needs updating if\
  `allocator_traits<Allocator>::propagate_on_container_move_assignment::value`\
  is `true`.

  6.2. If `other` is valueless, `*this` becomes valueless.
  
  6.3 Otherwise, if the allocator needs updating or `alloc == other.alloc` is `true`, `*this`
  takes ownership of the owned object of `other`.

  6.4 Otherwise, constructs a new owned object of type `U`, where `U` is the type of the owned object in `other`,
  with the owned object of `other` as the argument as an rvalue, using the allocator in `*this`.

  6.4. The previously owned object in `*this`, if any, is destroyed using `allocator_traits<Allocator>::destroy`
  and then the storage is deallocated.

  6.5. If the allocator needs updating, the allocator in `*this` is replaced with a copy of the
  allocator in `other`.

7. _Returns_: A reference to `*this`.

8. _Remarks_: If any exception is thrown, there are no effects on `*this` or `other`.

#### X.Y.6 Valued state queries [protocol.observers]

```cpp
constexpr bool valueless_after_move() const noexcept;
```

1. _Returns_: `true` if `*this` is valueless after a move, otherwise `false`.

```c++
constexpr allocator_type get_allocator() const noexcept;
```

2. _Returns_: `alloc`.

#### X.Y.7 Swap [protocol.swap]

```cpp
constexpr void swap(protocol& other) noexcept(
  allocator_traits<Allocator>::propagate_on_container_swap::value ||
  allocator_traits<Allocator>::is_always_equal::value);
```

1. _Preconditions_: If `allocator_traits<Allocator>​::​propagate_on_container_swap​::​value` is `true`,
then `Allocator` meets the _Cpp17Swappable_ requirements. Otherwise `get_allocator() == other`.
`get_allocator()` is `true`.

2. _Effects_: Swaps the states of `*this` and `other`, exchanging owned objects or valueless states.
If `allocator_traits<Allocator>​::​propagate_on_container_swap​::​value` is true, then the allocators of
`*this` and `other` are exchanged by calling swap as described in [swappable.requirements]. Otherwise, 
the allocators are not swapped.

[Note 1: Does not call swap on the owned objects directly. — end note]

3. _Remarks_: This function is a no-op if both arguments are valueless before the call.

<!--
```cpp
constexpr void swap(protocol& lhs, protocol& rhs) noexcept(noexcept(lhs.swap(rhs)));
```

4. _Effects_: Equivalent to lhs.swap(rhs).

??

```cpp
template<class I, class Allocator>
void swap(protocol<I, Allocator>& x, protocol<I, Allocator>& y)
  noexcept(noexcept(x.swap(y)));
```

4. _Effects_: Calls `x.swap(y)`.
-->

<!--
#### X.Y.9 Allocator support [protocol.alloc]

```cpp
constexpr allocator_type get_allocator() const;
```

1. _Returns_: `alloc`.
-->

### X.Z Class template protocol_view [protocol_view]

#### X.Z.1 Class template protocol_view general [protocol_view.general]

1. A `protocol_view` object provides a non-owning reference to an object that conforms to an 
interface `I`. Copying a `protocol_view` produces a new view referring to the same object.

2. An object of type `T` _conforms to an interface_ `I` if all member functions declared in `I` are 
available on `T` with matching signatures and are not deleted. Const-qualifiers in the interface's 
member functions are required to match.

3. A program that instantiates the definition of the template `protocol_view<I>`
with a type for the `I` parameter that is a non-object type, an array type, or a cv-qualified type is ill-formed.

4. The template parameter `I` of `protocol_view` shall be a complete type. The program is ill-formed if a 
type instantiates `protocol_view<I>` with an incomplete type `I`.

5. If a program declares an explicit or partial specialization of `protocol_view`,
the behavior is undefined.

#### X.Z.2 Class template protocol_view synopsis [protocol_view.syn]

```cpp
template <class I>
class protocol_view {
 public:
  using interface_type = I;

  template<class T>
  constexpr protocol_view(T& t) noexcept;
  
  template<class T>
  constexpr protocol_view(const T& t) noexcept;

  template<class Allocator>
  constexpr protocol_view(protocol<I, Allocator>& p) noexcept;

  template<class Allocator>
  constexpr protocol_view(const protocol<I, Allocator>& p) noexcept;
  
  template<class U>
  constexpr protocol_view(protocol_view<U>& view) noexcept;
  
  template<class U>
  constexpr protocol_view(const protocol_view<U>& view) noexcept;

  constexpr protocol_view(const protocol_view&) noexcept = default;

  constexpr protocol_view(protocol_view&&) noexcept = default;

  constexpr protocol_view& operator=(const protocol_view&) noexcept = default;

 private:
  I* data_; // exposition only
};
```

#### X.Z.3 Constructors [protocol_view.ctor]

```cpp
template<class T>
constexpr protocol_view(T& t) noexcept;
```

1. _Constraints_: `T` conforms to interface `I`.

2. _Preconditions_: `t` shall refer to an object that is valid and remains valid for the lifetime of `*this`.

3. _Effects_: Initializes `data_` to `std::addressof(t)`.

```cpp
template<class T>
constexpr protocol_view(const T& t) noexcept;
```

4. _Constraints_: `T` conforms to interface `I`.

5. _Preconditions_: `t` shall refer to an object that is valid and remains valid for the lifetime of `*this`.

6. _Effects_: Initializes `data_` to `const_cast<I*>(std::addressof(t))`.

```cpp
template<class Allocator>
constexpr protocol_view(protocol<I, Allocator>& p) noexcept;
```

X. _Preconditions_: ??

7. _Effects_: Initializes `data_` to `std::addressof(*p)`.

```cpp
template<class Allocator>
constexpr protocol_view(const protocol<I, Allocator>& p) noexcept;
```

8. _Effects_: Initializes `data_` to `const_cast<I*>(std::addressof(*p))`. 

[Note: The `const_cast` is safe because when `I` is `const`-qualified, only `const` member functions will be accessible. — end note]

```cpp
template<class U>
constexpr protocol_view(protocol_view<U>& view) noexcept;
```

9. _Constraints_: `const U` is implicitly convertible to `const I` (allowing different `const`-qualifications of the same interface).

10. _Preconditions_: The object referenced by `view` does not become invalid before use of `*this`.

11. _Effects_: Initializes `data_` to `view.operator->()`.

```cpp
template<class U>
constexpr protocol_view(const protocol_view<U>& view) noexcept;
```

12. _Constraints_: `const U` is implicitly convertible to `const I` (allowing different `const`-qualifications of the same interface).

13. _Preconditions_: The object referenced by `view` does not become invalid before use of `*this`.

14. _Effects_: Initializes `data_` to `const_cast<I*>(view.operator->())`.

#### X.Z.3 Copy and move operations [protocol_view.copy.move]

```cpp
constexpr protocol_view(const protocol_view& other) noexcept = default;
```

1. _Effects_: Initializes `data_` and `vtable` with the values from `other`.

```cpp
constexpr protocol_view(protocol_view&& other) noexcept = default;
```

2. _Effects_: Initializes `data_` and `vtable` with the values from `other`. [Note: The moved-from `protocol_view` remains valid and points to the same object. — end note]

```cpp
constexpr protocol_view& operator=(const protocol_view& other) noexcept = default;
```

3. _Effects_: Assigns `data_` and `vtable` from `other`.

4. _Returns_: `*this`.

```cpp
constexpr protocol_view& operator=(protocol_view&& other) noexcept = default;
```

5. _Effects_: Assigns `data_` and `vtable` from `other`. 

6. _Returns_: `*this`.

#### X.Z.4 Member access

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

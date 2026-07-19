# C++ Protocol Reference Implementation Notes

This document details the design and implementation of the `protocol` and `protocol_view` types, focusing on code generation, virtual dispatch, narrowing conversions, and concurrent safety. Sections 1-4 describe the default, Python/libclang build-step backend; section 5 describes the opt-in C++26-reflection backend, which reuses the narrowing and concurrency machinery in sections 3-4 unchanged.

---

## 1. Code Generation via Clang AST

Specializations of `protocol` and `protocol_view` are generated from user-defined interface structures by [scripts/generate_protocol.py](file:///workspace/scripts/generate_protocol.py) using the Jinja2 template [scripts/protocol.j2](file:///workspace/scripts/protocol.j2).

### AST Parsing
The generator uses `libclang` Python bindings (`clang.cindex`) to parse the target header file. It traverses the AST to construct a model of the C++ class, identifying all public non-virtual, non-template member functions. It extracts function attributes including the name, constness, exception specifications (`noexcept`), return types, and parameter types.

Interfaces must consist only of public, non-virtual, non-template member functions. Template member functions are not supported because they cannot be mapped to a fixed-size vtable struct at compile-time. During generation, the script automatically parses dependent system headers by querying the host compiler's include paths; however, custom flags must be passed to the clang parser if interfaces rely on external project headers.

### Name Mangling and Symbol Stability
To prevent symbol name collisions in the generated structs, member function pointers in the vtable must be uniquely identified. The generator produces a stable suffix by computing the MD5 hash of the function signature (e.g. `func2(int,int)`) and taking the first 8 characters:
$$\text{Suffix} = \text{MD5}(\text{signature})[0..7]$$
For overloaded functions, the signature string hashed to generate the suffix includes the full parameter list and constness qualifiers (for example, `write(int)const` versus `write(double)const`). This guarantees that overloaded functions produce distinct stable suffixes and separate vtable slots.

For example, `int func2(int)` generates the member `func2_0087aeab`. Pointers to these members remain stable and deterministic across compiler versions and independent generation runs.

---

## 2. Manual Vtables and Member Function Invocation

The implementation avoids compiler-generated virtual tables (`vtable`/`vptr`) to enforce value semantics, control layout constraints, and avoid runtime inheritance. Instead, it uses custom C++ structures of function pointers.

### Vtable Layout
For each interface, the generator produces two vtable layouts:
- `const_view_vtable_<Protocol>` holds function pointers mapping const member functions.
- `view_vtable_<Protocol>` holds a nested `const_view_vtable_<Protocol>` member followed by function pointers for non-const member functions.

Function pointer signatures take a type-erased pointer (`const void*` or `void*`) as the first argument, followed by the function parameters.

### Vtable Specialization
For a concrete type `T`, static constexpr instances `const_view_vtable_for<T>` and `view_vtable_for<T>` are initialized with lambdas that cast the type-erased pointer back to the concrete type:
```cpp
[](const void* ptr, Args... args) -> Ret {
    return static_cast<const T*>(ptr)->member_function(args...);
}
```

### Invocation Path
`protocol_view` stores a type-erased pointer `ptr_` and a pointer to the generated vtable `vptr_`. Calling a member function performs a single indirection:
```cpp
vptr_->member_function_mangled(ptr_, args...);
```
Because vtable pointers point to statically allocated, immutable structs (`const_view_vtable_for<T>`), this is identical to a standard virtual call cost but without class hierarchy coupling.

---

## 3. Narrowing Conversions (Subtype Substitution)

A `protocol` or `protocol_view` for interface `A` can be converted to one for interface `B` if `B` is a subset (subtype) of `A`.

### Constructor Constraints
Type traits `is_protocol` and `is_protocol_view` along with the concept `not_protocol_or_view` prevent concrete constructors from matching view/protocol types during conversions, avoiding recursion or self-wrapping.

### Converting Views
Conversions are enabled via templated copy constructors constrained by the target vtable size and layout compatibility:
```cpp
template <typename Other>
  requires (!std::same_as<Other, TargetProtocol>)
constexpr protocol_view(const protocol_view<Other>& other)
    : ptr_(other.ptr_),
      vptr_(get_mutable_vtable<Other, TargetProtocol>(other.vptr_)) {}
```
Conversions are fully transitive (for example, `protocol_view<A>` to `protocol_view<B>` to `protocol_view<C>`). In each step, the registry maps the current vtable pointer to the target interface vtable. Since the mapping registry resolves type transitions directly, intermediate conversions do not create chain-linked redirects.

### Converting Owning Protocols
Allocator-extended and standard converting constructors construct the target `protocol` from the source `protocol`. If the allocators are equal, the storage pointer `p_` is moved directly (`std::exchange`) and the target vtable is mapped. If the allocators are not equal, the source's `xyz_protocol_move` or `xyz_protocol_clone` function is called to construct the value in the target allocator's storage.

---

## 4. Vtable Registry & Concurrency

When narrowing from `Other` to `Target`, a new vtable matching `Target`'s layout must be built and populated with function pointers extracted from `Other`'s vtable. This mapping occurs dynamically inside a global type-erased registry.

### Registry Signature
```cpp
const void* get_mapped_vtable(
    const void* source_vtable_pointer, const void* conversion_anchor,
    std::size_t target_vtable_size,
    void (*mapping_function)(const void* source, void* target));
```

### The Cache and Lifetime Control (Intentional Leak)
Mapped vtables are cached in a static `std::unordered_map` keyed by `CacheKey{source_vtable_pointer, conversion_anchor}`. The `conversion_anchor` is the address of a static template local `conversion_anchor`, ensuring target vtable/allocator uniqueness. Values are stored as `std::unique_ptr<char[]>`. Because the map is node-allocated, returned pointers to elements remain stable.

To ensure safety during program shutdown, the cache map and its protecting mutex are initialized as dynamic objects allocated via `new` on the heap and referenced statically (`static auto& cache = *new ...`). This deliberately prevents their destruction during program termination, avoiding Undefined Behavior (such as segfaults) if other global or static objects trigger protocol conversions during cleanup/destructor execution.

Because active references to these static structures reside in the global data segment throughout the application runtime, Address Sanitizer's Leak Sanitizer (LSAN) classifies them as reachable memory rather than a leak, passing all sanitizer checks on exit without needing suppression files.

Since the vtables are dynamically allocated and retained on the heap until program termination, memory growth is bounded by the total number of distinct conversion type pairs in the binary. This compile-time bound ensures that the cache does not require an eviction policy (such as LRU) or memory cap, as memory consumption remains flat after startup.

Pointer equality is used to compare the `CacheKey` components. This is safe because static vtable instances and anchor variables are guaranteed to have unique heap or data segment addresses. Compiler optimization techniques (such as COMDAT folding or duplicate variable consolidation) do not affect correctness because identical layouts that are folded share identical function pointer semantics.

### Split-Lock Pattern
To prevent recursive deadlocks when nested conversions occur (e.g. mapping an owning vtable requires mapping its nested mutable vtable on the same thread), the mutex is not held during mapping.

While the conversion is an O(1) pointer assignment on a cache hit, the very first conversion for a given type pair incurs a cold-start overhead due to the mutex lock, cache lookup, buffer allocation, and mapping. The conversions are therefore described as amortized zero-cost.

The lookup and population sequence is:
1. Lock the mutex and look up the key. If found, return the pointer and unlock the mutex.
2. If it is a cache miss, unlock the mutex.
3. Allocate the target vtable buffer in thread-local storage.
4. Invoke `mapper()` to populate the new vtable.
5. Lock the mutex and attempt to insert the buffer using `cache.emplace()`.
6. If the insertion succeeds, publish and return the pointer.
7. If the insertion fails (meaning another thread inserted the key concurrently), the local buffer is destroyed, and the already-cached pointer is returned.

This guarantees that all threads always resolve to the identical vtable pointer for a given conversion key, eliminating data races and leaks under high contention.

---

## 5. C++26 Reflection Code-Generation Backend

[protocol_reflection.h](protocol_reflection.h) is an opt-in second backend that generates the same machinery as sections 1-2 above, but inside the compiler at compile time via C++26 reflection ([P2996R13]), instead of ahead of compilation via `libclang` and a Jinja2 template. It requires GCC 16+ (`-std=c++26 -freflection`) and the CMake option `XYZ_PROTOCOL_ENABLE_REFLECTION_BACKEND`. [protocol_reflection_guide.md](protocol_reflection_guide.md) is a full section-by-section walkthrough of the header for anyone about to read or modify it; this section gives the shorter design summary in the style of sections 1-4.

### Reflection Primitives

Three operations cover almost all of what the backend needs. `^^Type` lifts a type, function, or data member into a `std::meta::info` value. A library of `consteval std::meta::*` functions (`is_function`, `is_const`, `identifier_of`, `return_type_of`, and similar) then answers ordinary questions about that `info`. A splice, `[:member:]`, lowers an `info` back into code: `object.[:member:]()` calls the member it reflects, and `typename [:type:]` names the type it reflects.

### Interface Enumeration and Vtable Generation

`is_interface_member_function` and `interface_member_functions` enumerate an interface's public, non-static, non-special member functions in declaration order, playing the same role as the AST traversal in section 1 (the same restriction on template member functions applies, for the same reason: no fixed signature to give a vtable slot). Entry names are mangled as `<name>_<hash-of-signature>`, using a `consteval` FNV-1a hash over the signature instead of section 1's MD5, for the same purpose: deterministic, overload-disambiguating symbol names.

Rather than rendering a vtable struct from a template, `define_vtable_entries` builds a list of `data_member_spec`s (one function pointer per interface member) and calls `std::meta::define_aggregate` to complete an incomplete struct with them at compile time. `view_vtables<Interface>` and `owning_vtable<Interface,Allocator>` wrap that generated `entries` struct in a handwritten shell that adds the fixed lifetime operations (`xyz_protocol_clone`/`move`/`destroy`), mirroring the two vtable layouts in section 2. Populating a vtable for a given `(Interface, Implementation)` pair resolves each interface member against the stored type (see below) and stores the address of an `erased_call_thunk::call` specialization, which casts the erased pointer back to `Implementation*` and splice-calls the resolved member — the reflection equivalent of section 2's cast-and-call lambdas.

### Duck-Typed Member Resolution

`resolve_implementation_member` looks for exactly one member of the implementation type matching an interface member by name (or operator kind), constness, and exact parameter types after alias stripping, with a convertible return type; `models_reflected_interface` applies this to every interface member and backs `reflection_protocol_const_concept`/`reflection_protocol_concept`, the reflection equivalents of the Python backend's generated `protocol_concept_<Name>`. This is a documented approximation of full overload resolution (see known limitations below), evaluated when `protocol<T>` is instantiated with a stored type rather than emitted as generated `requires`-clauses.

### Giving Vtable Entries Call Syntax

C++26 reflection cannot splice a reflection as the name of a *new* declaration, so a member function actually named `name` cannot be spliced into existence the way a vtable entry's mangled name can. The backend works around this using the technique from Ryan Keane's `rjk::duck` library: since `define_aggregate` can declare a data member from a plain string, each interface member gets a data member (not a function) named after it, whose type is an empty `forwarding_call` wrapper with an exact-signature `operator()`. Because `a.name()` doesn't distinguish a member function from a data member whose type has `operator()`, this gives ordinary call syntax for free. The wrapper's `operator()` recovers its owning `protocol`/`protocol_view` via `static_cast<Owner*>(static_cast<void*>(this))`, valid only because every wrapper sits at offset zero of its owner; `forwarders_at_offset_zero` checks that layout assumption with `std::meta::offset_of` rather than assuming it. Operators (`operator+`, etc.) can't use this trick, since a data member can't be named `operator+`; each operator kind instead gets its own macro-stamped forwarder template with the operator symbol written literally in source (38 invocations, one per supported operator kind).

### Narrowing and Concurrency

Sections 3 and 4 above apply unchanged: `protocol_vtable_traits<T>` and `protocol_owning_vtable_traits<T,Allocator>` specializations plug the reflection-generated vtable types into the same `get_vtable`/`get_mutable_vtable`/`get_owning_vtable` registry that section 4 describes, so narrowing conversions, the vtable cache, and its concurrency guarantees are identical regardless of which backend produced the vtable being narrowed.

### Conformance Is Exact, By Design

A stored type that overloads an interface method's name must have one overload with an exactly matching parameter-type list after alias stripping; this backend does not perform full overload resolution with implicit conversions. This matches the design in `DRAFT.md`, which requires exact signature matching for conformance specifically to rule out conformance via chains of implicit conversions. The Python backend's generated `requires`-expression instead calls through ordinary overload resolution, so it is the more permissive of the two backends here, not the other way around.

### Known Limitations

Interface members must be direct members of the stored type — members inherited from a base of the implementation are not found. An interface member function named `swap` or `valueless_after_move` would be silently hidden by `protocol`'s own member of that name (since the generated forwarders are inherited data members), so it is instead rejected with a `static_assert` naming the interface. Interfaces with several assignment operators of the same constness are not supported, since assignment forwarding requires a unique target per constness. Generated member functions are not marked `constexpr`, which is out of scope for this backend for now.

[P2996R13]: https://isocpp.org/files/papers/P2996R13.html

# C++ Protocol Reference Implementation Notes

This document details the design and implementation of the `protocol` and `protocol_view` types, focusing on code generation, virtual dispatch, narrowing conversions, and concurrent safety.

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

# C++ Protocol Reference Implementation Notes

This document details the design and implementation of the `protocol` and `protocol_view` types, focusing on code generation, virtual dispatch, narrowing conversions, and concurrent safety.

---

## 1. Code Generation via Clang AST

Specializations of `protocol` and `protocol_view` are generated from user-defined interface structures by [scripts/generate_protocol.py](file:///workspace/scripts/generate_protocol.py) using the Jinja2 template [scripts/protocol.j2](file:///workspace/scripts/protocol.j2).

1. **AST Parsing:**
   * Uses `libclang` Python bindings (`clang.cindex`) to parse the target header file.
   * Traverses the AST to construct a model of the C++ class, identifying all public non-virtual, non-template member functions.
   * Extracts function attributes including: name, constness, exception specifications (`noexcept`), return types, and parameter types.

2. **Name Mangling and Symbol Stability:**
   * To prevent symbol name collisions in the generated structs, member function pointers in the vtable must be uniquely identified.
   * Generates a stable suffix by computing the MD5 hash of the function signature (e.g. `func2(int,int)`) and taking the first 8 characters:
     $$\text{Suffix} = \text{MD5}(\text{signature})[0..7]$$
   * Example: `int func2(int)` generates member `func2_0087aeab`.
   * Pointers to these members remain stable and deterministic across compiler versions and independent generation runs.

---

## 2. Manual Vtables and Member Function Invocation

The implementation avoids compiler-generated virtual tables (`vtable`/`vptr`) to enforce value semantics, control layout constraints, and avoid runtime inheritance. Instead, it uses custom C++ structures of function pointers.

1. **Vtable Layout:**
   * For each interface, the generator produces two vtable layouts:
     * `const_view_vtable_<Protocol>`: Holds function pointers mapping const member functions.
     * `view_vtable_<Protocol>`: Holds a nested `const_view_vtable_<Protocol>` member followed by function pointers for non-const member functions.
   * Function pointer signatures take a type-erased pointer (`const void*` or `void*`) as the first argument, followed by the function parameters.

2. **Vtable Specialization:**
   * For a concrete type `T`, static constexpr instances `const_view_vtable_for<T>` and `view_vtable_for<T>` are initialized with lambdas that cast the type-erased pointer back to the concrete type:
     ```cpp
     [](const void* ptr, Args... args) -> Ret {
         return static_cast<const T*>(ptr)->member_function(args...);
     }
     ```

3. **Invocation Path:**
   * `protocol_view` stores a type-erased pointer `ptr_` and a pointer to the generated vtable `vptr_`.
   * Calling a member function performs a single indirection:
     ```cpp
     vptr_->member_function_mangled(ptr_, args...);
     ```
   * Because vtable pointers point to statically allocated, immutable structs (`const_view_vtable_for<T>`), this is identical to a standard virtual call cost but without class hierarchy coupling.

---

## 3. Narrowing Conversions (Subtype Substitution)

A `protocol` or `protocol_view` for interface `A` can be converted to one for interface `B` if `B` is a subset (subtype) of `A`.

1. **Constructor Constraints:**
   * We define type traits `is_protocol` and `is_protocol_view` along with the concept `not_protocol_or_view`.
   * These traits prevent concrete constructors from matching view/protocol types during conversions, avoiding recursion or self-wrapping.

2. **Converting Views:**
   * Conversions are enabled via templated copy constructors constrained by the target vtable size and layout compatibility:
     ```cpp
     template <typename Other>
       requires (!std::same_as<Other, TargetProtocol>)
     constexpr protocol_view(const protocol_view<Other>& other)
         : ptr_(other.ptr_),
           vptr_(get_or_create_mutable_vtable<Other, TargetProtocol>(other.vptr_)) {}
     ```

3. **Converting Owning Protocols:**
   * Allocator-extended and standard converting constructors construct the target `protocol` from the source `protocol`.
   * If the allocators are equal, the storage pointer `p_` is moved directly (`std::exchange`) and the target vtable is mapped.
   * If the allocators are not equal, the source's `xyz_protocol_move` or `xyz_protocol_clone` function is called to construct the value in the target allocator's storage.

---

## 4. Vtable Registry & Concurrency

When narrowing from `Other` to `Target`, a new vtable matching `Target`'s layout must be built and populated with function pointers extracted from `Other`'s vtable. This mapping occurs dynamically inside a global type-erased registry.

1. **Registry Signature:**
   ```cpp
   const void* get_or_create_vtable_erased(
       const void* from_vptr, const void* type_id, std::size_t to_vtable_size,
       void (*mapper)(const void* from, void* to));
   ```

2. **The Cache:**
   * Caches mapped vtables in a static `std::unordered_map` keyed by `CacheKey{from_vptr, type_id}`.
   * `type_id` is the address of a static template local `type_id_anchor`, ensuring target vtable/allocator uniqueness.
   * Values are stored as `std::unique_ptr<char[]>`. Because the map is node-allocated, returned pointers to elements remain stable.

3. **Split-Lock Pattern:**
   * To prevent recursive deadlocks when nested conversions occur (e.g. mapping an owning vtable requires mapping its nested mutable vtable on the same thread), the mutex is **not** held during mapping.
   * **Sequence:**
     1. Lock mutex, lookup key. If found, return pointer, unlock.
     2. If miss, unlock mutex.
     3. Allocate target vtable buffer in thread-local storage.
     4. Invoke `mapper()` to populate the new vtable.
     5. Lock mutex, try to insert the buffer using `cache.emplace()`.
     6. If insertion succeeds, publish and return pointer.
     7. If insertion fails (another thread inserted the key during step 3-4), the local buffer is destroyed, and the already-cached pointer is returned.
   * This guarantees that all threads always resolve to the identical vtable pointer for a given conversion key, eliminating data races and leaks under high contention.

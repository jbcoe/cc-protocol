/* Copyright (c) 2025 The XYZ Protocol Authors. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
==============================================================================*/
#ifndef XYZ_PROTOCOL_VTABLE_HH_
#define XYZ_PROTOCOL_VTABLE_HH_

#include <cassert>
#include <meta>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "xyz/detail/conformance.hh"
#include "xyz/detail/name_mangling.hh"
#include "xyz/detail/overload_spec.hh"
#include "xyz/detail/protocol_traits.hh"
#include "xyz/detail/small_buffer.hh"

namespace xyz::detail {

// The mangled name of `Member`, computed once per distinct `Member`.
template <std::meta::info Member>
constexpr inline auto mangled_name_of =
    std::define_static_string(xyz::detail::mangle(Member));

// The `VtableType` entry named after the mangled member function `Member`.
template <std::meta::info VtableType, std::meta::info Member>
consteval std::meta::info find_vtable_entry() {
  std::string_view name = mangled_name_of<Member>;
  std::vector<std::meta::info> entries = nonstatic_data_members_of(
      VtableType, std::meta::access_context::unprivileged());
  for (std::meta::info entry : entries) {
    if (identifier_of(entry) == name) return entry;
  }
  throw std::runtime_error("find_vtable_entry: no entry named '" +
                           std::string(name) + "'");
}

// Calls the `Vtable` entry for `Member`. `Vtable` is named by the caller, not
// deduced: an owning vtable that derives from it converts at the call, so the
// type searched for the entry is the type the entry is spliced into.
template <std::meta::info Member, typename Vtable, typename ProtocolObject,
          typename Object, typename... Args>
[[gnu::always_inline]] inline decltype(auto) call_through_vtable(
    [[maybe_unused]] ProtocolObject* protocol_object, const Vtable* vtable,
    Object object, Args&&... args) {
  if constexpr (is_protocol_v<std::remove_cv_t<ProtocolObject>>) {
    assert(!valueless_after_move(*protocol_object) &&
           "cannot call member function of valueless protocol");
  }
  constexpr std::meta::info vtable_entry =
      find_vtable_entry<^^Vtable, Member>();
  return vtable->[:vtable_entry:](object, std::forward<Args>(args)...);
}

// Returns a list of data_member_spec values, one for each member function
// implemented by `protocol`, each describing a vtable function pointer with
// signature R(*)(void*, Args...) noexcept(...) for a mutable interface
// method, or R(*)(const void*, Args...) noexcept(...) for a const one, named
// by the member function's mangled signature (see `xyz::detail::mangle`).
template <std::meta::info interface_type>
consteval std::vector<std::meta::info> generate_vtable_specs() {
  std::vector<std::meta::info> function_pointer_specs;

  function_pointer_specs.push_back(data_member_spec(
      ^^const std::type_info*, {
                                   .name = "xyz_protocol_typeid"}));

  template for (constexpr std::meta::info member :
                protocol_interface_functions_of<interface_type>) {
    // A const method takes `const void*`, matching the constness of the
    // access path it's called through.
    std::meta::info fn_ptr_type = function_pointer_type_of(
        member, {is_const(member) ? ^^const void* : ^^void* });

    // GCC UBSAN workaround: `std::string`'s pointer-taking constructors have a
    // null check GCC trunk can't constant-fold under `-fsanitize=undefined`,
    // even though `mangled_name_of<member>` is never null. The iterator-pair
    // constructor has no such check.
    std::string_view cached_name = mangled_name_of<member>;
    function_pointer_specs.push_back(data_member_spec(
        fn_ptr_type,
        std::meta::data_member_options{
            .name = std::string(cached_name.begin(), cached_name.end())}));
  }
  return function_pointer_specs;
}

// Generates a vtable with named function pointers for each public,
// non-special, member function from `T`. Name mangling ensures that
// names for overloads are unique.
template <typename T>
struct vtable_generator {
  struct type;
  consteval { define_aggregate(^^type, generate_vtable_specs<^^T>()); }
};

template <typename T>
using vtable_t = typename vtable_generator<T>::type;

// Trampolines translate type-erased calls to vtable functions to member
// function calls on the underlying (owned or viewed) object.
//
// A direct trampoline is passed the object's address: a viewed object, or an
// owned object in a protocol's small buffer. `std::launder` is needed for the
// latter, as the address passed is that of the protocol's
// `small_buffer_storage`, and is a no-op for the former. An indirect
// trampoline is passed the address of a protocol's `small_buffer_storage`
// holding a pointer to a heap-allocated object.
template <typename FnPtrType, typename U, std::meta::info CandidateMember,
          bool Indirect = false>
struct mutable_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember, bool Indirect>
struct mutable_view_trampoline<R (*)(void*, Args...) noexcept(Noexcept), U,
                               CandidateMember, Indirect> {
  static R operator()(void* ptr, Args... args) noexcept(Noexcept) {
    U* object = nullptr;
    if constexpr (Indirect) {
      object =
          static_cast<U*>(static_cast<small_buffer_storage*>(ptr)->pointer_);
    } else {
      object = std::launder(static_cast<U*>(ptr));
    }
    return object->[:CandidateMember:](std::forward<Args>(args)...);
  }
};

template <typename FnPtrType, typename U, std::meta::info CandidateMember,
          bool Indirect = false>
struct const_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember, bool Indirect>
struct const_view_trampoline<R (*)(const void*, Args...) noexcept(Noexcept), U,
                             CandidateMember, Indirect> {
  static R operator()(const void* ptr, Args... args) noexcept(Noexcept) {
    const U* object = nullptr;
    if constexpr (Indirect) {
      object = static_cast<const U*>(
          static_cast<const small_buffer_storage*>(ptr)->pointer_);
    } else {
      object = std::launder(static_cast<const U*>(ptr));
    }
    return object->[:CandidateMember:](std::forward<Args>(args)...);
  }
};

// Builds a vtable for `T` whose entries call through to the corresponding
// member of `U`, using indirect trampolines if `Indirect`.
// For `protocol_view` some vtable entries may be unused due to const
// qualifiers. We opt for simple vtable construction logic and rely on
// generation of forwarding wrappers to determine which functions are forwarded.
template <typename T, typename U, bool Indirect = false>
consteval vtable_t<T> make_view_vtable() {
  vtable_t<T> vtable{};

  vtable.xyz_protocol_typeid = &typeid(U);

  template for (constexpr std::meta::info member :
                protocol_interface_functions_of<^^T>) {
    constexpr std::meta::info vtable_member =
        find_vtable_entry<^^vtable_t<T>, member>();
    using FnPtrType = typename[:type_of(vtable_member):];
    if constexpr (is_const(member)) {
      constexpr std::meta::info candidate =
          find_conforming_member<member, ^^U>();
      vtable.[:vtable_member:] = &const_view_trampoline<FnPtrType, U, candidate,
                                                        Indirect>::operator();
    } else /*constexpr*/ {
      constexpr std::meta::info candidate =
          find_conforming_member<member, ^^U>();
      vtable
          .[:vtable_member:] = &mutable_view_trampoline<FnPtrType, U, candidate,
                                                        Indirect>::operator();
    }
  }
  return vtable;
}

// The shared, compile-time vtable every protocol_view<T> (or
// protocol_view<const T>) that views a `U` points to.
template <typename T, typename U>
inline constexpr vtable_t<T> view_vtable_for = make_view_vtable<T, U>();

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_VTABLE_HH_

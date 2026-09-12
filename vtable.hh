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

#include <meta>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include "conformance.hh"
#include "name_mangling.h"
#include "overload_spec.hh"

namespace xyz::detail {

// The mangled name of `Member`, computed once per distinct `Member`.
template <std::meta::info Member>
constexpr inline auto mangled_name_of =
    std::define_static_string(xyz::name_mangling::mangle(Member));

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

// Returns a list of data_member_spec values, one for each member function
// implemented by `protocol`, each describing a vtable function pointer with
// signature R(*)(void*, Args...) for a mutable interface method, or
// R(*)(const void*, Args...) for a const one, named by the member function's
// mangled signature (see `xyz::name_mangling::mangle`).
template <std::meta::info interface_type>
consteval std::vector<std::meta::info> generate_vtable_specs() {
  std::vector<std::meta::info> function_pointer_specs;

  function_pointer_specs.push_back(data_member_spec(
      ^^const std::type_info*, {
                                   .name = "xyz_protocol_typeid"}));

  template for (constexpr std::meta::info member :
                protocol_interface_functions_of<interface_type>) {
    // Build the function-pointer type R(*)(void*, Args...) noexcept(...)
    // from the method's return type, parameter types and noexcept-ness; a
    // const method takes `const void*` instead, matching the constness of
    // the access path it's called through.
    std::vector<std::meta::info> fn_args{dealias(return_type_of(member))};
    fn_args.push_back(is_const(member) ? ^^const void* : ^^void*);
    std::vector<std::meta::info> member_parameters = parameters_of(member);
    for (std::meta::info parameter : member_parameters) {
      fn_args.push_back(dealias(type_of(parameter)));
    }
    std::meta::info fn_ptr_type = substitute(^^fn_ptr_t, fn_args);

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
template <typename FnPtrType, typename U, std::meta::info CandidateMember>
struct mutable_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember>
struct mutable_view_trampoline<R (*)(void*, Args...) noexcept(Noexcept), U,
                               CandidateMember> {
  static R operator()(void* ptr, Args... args) noexcept(Noexcept) {
    return static_cast<U*>(ptr)->[:CandidateMember:](
        std::forward<Args>(args)...);
  }
};

template <typename FnPtrType, typename U, std::meta::info CandidateMember>
struct const_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember>
struct const_view_trampoline<R (*)(const void*, Args...) noexcept(Noexcept), U,
                             CandidateMember> {
  static R operator()(const void* ptr, Args... args) noexcept(Noexcept) {
    return static_cast<const U*>(ptr)->[:CandidateMember:](
        std::forward<Args>(args)...);
  }
};

// Builds a vtable for `T` whose entries call through to the corresponding
// member of `U`.
// For `protocol_view` some vtable entries may be unused due to const
// qualifiers. We opt for simple vtable construction logic and rely on
// generation of forwarding wrappers to determine which functions are forwarded.
template <typename T, typename U>
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
      vtable.[:vtable_member:] = &const_view_trampoline<FnPtrType, U,
                                                        candidate>::operator();
    } else /*constexpr*/ {
      constexpr std::meta::info candidate =
          find_conforming_member<member, ^^U>();
      vtable.[:vtable_member:] = &mutable_view_trampoline<
                                   FnPtrType, U, candidate>::operator();
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

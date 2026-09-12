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
#ifndef XYZ_PROTOCOL_MEMBER_FUNCTION_THUNKS_HH_
#define XYZ_PROTOCOL_MEMBER_FUNCTION_THUNKS_HH_

#include <cassert>
#include <meta>
#include <ranges>
#include <utility>
#include <vector>

#include "overload_spec.hh"
#include "protocol_traits.hh"
#include "vtable.hh"

namespace xyz::detail {

// Vanishing-this-pointer thunk for one overload of a synthesised named
// member function. A generated `member_base` holds it as its sole data
// member, named after the interface method, giving
// `p.member_function_name(args)` call syntax.
template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst,
          bool IsNoexcept>
struct member_function_thunk;

template <typename R, typename... Args, typename EnclosingType,
          typename ProtocolType, typename Vtable, std::meta::info Member,
          bool IsConst, bool IsNoexcept>
struct member_function_thunk<R (*)(Args...), EnclosingType, ProtocolType,
                             Vtable, Member, IsConst, IsNoexcept> {
  static constexpr std::meta::info vtable_entry =
      find_vtable_entry<^^Vtable, Member>();

  R operator()(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* enclosing = reinterpret_cast<EnclosingType*>(this);
    auto* protocol_object = static_cast<ProtocolType*>(enclosing);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_,
                                    std::forward<Args>(args)...);
  }

  R operator()(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* enclosing = reinterpret_cast<const EnclosingType*>(this);
    const auto* protocol_object = static_cast<const ProtocolType*>(enclosing);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_,
                                    std::forward<Args>(args)...);
  }

 protected:
  // Only `member_function_overload_set` may create or copy a thunk.
  member_function_thunk() = default;
  ~member_function_thunk() = default;
  member_function_thunk(const member_function_thunk&) = default;
  member_function_thunk(member_function_thunk&&) = default;
  member_function_thunk& operator=(const member_function_thunk&) = default;
  member_function_thunk& operator=(member_function_thunk&&) = default;
};

// The `member_function_thunk` specialisation for an `overload_spec`.
template <typename OverloadSpec, typename EnclosingType, typename ProtocolType,
          typename Vtable>
struct member_function_thunk_for;

template <std::meta::info Member, bool IsConst, typename EnclosingType,
          typename ProtocolType, typename Vtable>
struct member_function_thunk_for<overload_spec<Member, IsConst>, EnclosingType,
                                 ProtocolType, Vtable> {
  // Build the function-pointer type R(*)(Args...) from the method's return
  // type and parameter types.
  static consteval std::meta::info fn_ptr_type() {
    std::vector<std::meta::info> fn_args{dealias(return_type_of(Member))};
    fn_args.append_range(parameters_of(Member) |
                         std::views::transform(std::meta::type_of));
    return substitute(^^fn_ptr_t, fn_args);
  }

  // clang-format off
  using type = typename[:substitute(
      ^^member_function_thunk, {fn_ptr_type(), ^^EnclosingType, ^^ProtocolType, ^^Vtable,
                       std::meta::reflect_constant(Member),
                       std::meta::reflect_constant(IsConst),
                       std::meta::reflect_constant(is_noexcept(Member))}):];
  // clang-format on
};

template <typename Spec, typename EnclosingType, typename ProtocolType,
          typename Vtable>
using member_function_thunk_t =
    member_function_thunk_for<Spec, EnclosingType, ProtocolType, Vtable>::type;

// The overload set for one synthesised named member function: a
// `member_function_thunk` per overload, with every operator() brought into
// scope so that overload resolution among them works as for a member function
// of the interface.
template <typename EnclosingType, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct member_function_overload_set
    : member_function_thunk_t<OverloadSpecs, EnclosingType, ProtocolType,
                              Vtable>... {
  using member_function_thunk_t<OverloadSpecs, EnclosingType, ProtocolType,
                                Vtable>::operator()...;

 private:
  friend EnclosingType;
  member_function_overload_set() = default;
  ~member_function_overload_set() = default;
  member_function_overload_set(const member_function_overload_set&) = default;
  member_function_overload_set(member_function_overload_set&&) = default;
  member_function_overload_set& operator=(const member_function_overload_set&) =
      default;
  member_function_overload_set& operator=(member_function_overload_set&&) =
      default;
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_MEMBER_FUNCTION_THUNKS_HH_

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
#ifndef XYZ_PROTOCOL_OPERATOR_THUNKS_HH_
#define XYZ_PROTOCOL_OPERATOR_THUNKS_HH_

#include <cassert>
#include <meta>
#include <ranges>
#include <utility>
#include <vector>

#include "overload_spec.hh"
#include "protocol_traits.hh"
#include "vtable.hh"

namespace xyz::detail {

// Thunk for one overload of a synthesised call operator. `operator()` can't
// be reached through a named member, so `operator_overload_set`
// derives from this thunk directly instead of holding it as a data member,
// letting `ProtocolType` be recovered with a plain static_cast.
template <std::meta::operators Operator, typename FnPtrType,
          typename ProtocolType, typename Vtable, std::meta::info Member,
          bool IsConst, bool IsNoexcept>
struct operator_thunk {
  operator_thunk() =
      delete ("Unspecialized operator thunk cannot be instantiated");
};

// operator()
template <typename R, typename... Args, typename ProtocolType, typename Vtable,
          std::meta::info Member, bool IsConst, bool IsNoexcept>
struct operator_thunk<std::meta::operators::op_parentheses, R (*)(Args...),
                      ProtocolType, Vtable, Member, IsConst, IsNoexcept> {
  static constexpr std::meta::info vtable_entry =
      find_vtable_entry<^^Vtable, Member>();

  R operator()(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
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
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_,
                                    std::forward<Args>(args)...);
  }
};

// operator []
template <typename R, typename... Args, typename ProtocolType, typename Vtable,
          std::meta::info Member, bool IsConst, bool IsNoexcept>
struct operator_thunk<std::meta::operators::op_square_brackets, R (*)(Args...),
                      ProtocolType, Vtable, Member, IsConst, IsNoexcept> {
  static constexpr std::meta::info vtable_entry =
      find_vtable_entry<^^Vtable, Member>();

  R operator[](Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_,
                                    std::forward<Args>(args)...);
  }

  R operator[](Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_,
                                    std::forward<Args>(args)...);
  }
};

// operator ->
template <typename R, typename ProtocolType, typename Vtable,
          std::meta::info Member, bool IsConst, bool IsNoexcept>
struct operator_thunk<std::meta::operators::op_arrow, R (*)(), ProtocolType,
                      Vtable, Member, IsConst, IsNoexcept> {
  static constexpr std::meta::info vtable_entry =
      find_vtable_entry<^^Vtable, Member>();

  R operator->() noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_);
  }

  R operator->() const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_);
  }
};

// operator *
template <typename R, typename ProtocolType, typename Vtable,
          std::meta::info Member, bool IsConst, bool IsNoexcept>
struct operator_thunk<std::meta::operators::op_star, R (*)(), ProtocolType,
                      Vtable, Member, IsConst, IsNoexcept> {
  static constexpr std::meta::info vtable_entry =
      find_vtable_entry<^^Vtable, Member>();

  R operator*() noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_);
  }

  R operator*() const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    if constexpr (xyz::reflection::is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_);
  }
};

// The `operator_thunk` specialisation for an `overload_spec`.
template <typename OverloadSpec, typename ProtocolType, typename Vtable>
struct operator_thunk_for;

template <std::meta::info Member, bool IsConst, typename ProtocolType,
          typename Vtable>
struct operator_thunk_for<overload_spec<Member, IsConst>, ProtocolType,
                          Vtable> {
  // Build the function-pointer type R(*)(Args...) from the method's return
  // type and parameter types.
  static consteval std::meta::info fn_ptr_type() {
    std::vector<std::meta::info> fn_args{dealias(return_type_of(Member))};
    fn_args.append_range(parameters_of(Member) |
                         std::views::transform(std::meta::type_of));
    return substitute(^^fn_ptr_t, fn_args);
  }

  // clang-format off
  using type =
      typename[:substitute(^^operator_thunk,
                    {
                        std::meta::reflect_constant(operator_of(Member)),
                        fn_ptr_type(),
                         ^^ProtocolType, ^^Vtable,
                        std::meta::reflect_constant(Member),
                        std::meta::reflect_constant(IsConst),
                        std::meta::reflect_constant(is_noexcept(Member))
                    }):];
  // clang-format on
};

template <typename OverloadSpec, typename ProtocolType, typename Vtable>
using operator_thunk_t =
    operator_thunk_for<OverloadSpec, ProtocolType, Vtable>::type;

// The overload set for a synthesised operator X inheriting from an
// `operator_thunk` for each overload.
template <std::meta::operators Operator, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct operator_overload_set {
  operator_overload_set() =
      delete ("Unspecialized operator overload set cannot be instantiated");
};

// operator()
template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_parentheses, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator()...;
};

// operator[]
template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_square_brackets,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator[]...;
};

// operator->
template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_arrow, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator->...;
};

// operator*
template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_star, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator*...;
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_OPERATOR_THUNKS_HH_

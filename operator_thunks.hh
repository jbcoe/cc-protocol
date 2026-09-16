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

#include <meta>
#include <ranges>
#include <utility>
#include <vector>

#include "overload_spec.hh"
#include "vtable.hh"

namespace xyz::detail {

// Thunk for one overload of a synthesised call operator. `operator()` can't
// be reached through a named member, so `operator_overload_set`
// derives from this thunk directly instead of holding it as a data member,
// letting `ProtocolType` be recovered with a plain static_cast.
template <std::meta::operators Operator, typename FnPtrType,
          typename ProtocolType, typename Vtable, std::meta::info Member,
          bool IsConst>
struct operator_thunk {
  static_assert(false, "Unspecialized operator_thunk cannot be instantiated.");
};

// The overload set for a synthesised operator X inheriting from an
// `operator_thunk` for each overload.
template <std::meta::operators Operator, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct operator_overload_set {
  static_assert(false,
                "Unspecialized operator_overload_set cannot be instantiated.");
};

// The `operator_thunk` specialisation for an `overload_spec`.
template <typename OverloadSpec, typename ProtocolType, typename Vtable>
struct operator_thunk_for;

template <std::meta::info Member, bool IsConst, typename ProtocolType,
          typename Vtable>
struct operator_thunk_for<overload_spec<Member, IsConst>, ProtocolType,
                          Vtable> {
  // Build the function-pointer type R(*)(Args...) noexcept(...) from the
  // method's return type, parameter types and noexcept-ness.
  static consteval std::meta::info fn_ptr_type() {
    std::vector<std::meta::info> fn_args{
        std::meta::reflect_constant(is_noexcept(Member)),
        dealias(return_type_of(Member))};
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
                        std::meta::reflect_constant(IsConst)
                    }):];
  // clang-format on
};

template <typename OverloadSpec, typename ProtocolType, typename Vtable>
using operator_thunk_t =
    operator_thunk_for<OverloadSpec, ProtocolType, Vtable>::type;

// operator()
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_parentheses,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator()(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator()(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_parentheses, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator()...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator []
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_square_brackets,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator[](Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator[](Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_square_brackets,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator[]...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator ->
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_arrow,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator->() noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_);
  }

  R operator->() const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_arrow, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator->...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator *
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_star,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator*(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator*(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_star, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator*...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator->*
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_arrow_star,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator->*(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator->*(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_arrow_star, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator->*...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator~: always unary as a member (the language forbids a parameter),
// so this uses a fixed R(*)() rather than the generic R(*)(Args...) shape.
template <typename R, bool IsNoexcept, typename ProtocolType, typename Vtable,
          std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_tilde,
                      R (*)() noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator~() noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_);
  }

  R operator~() const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_tilde, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator~...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator!: always unary as a member, for the same reason as operator~.
template <typename R, bool IsNoexcept, typename ProtocolType, typename Vtable,
          std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_exclamation,
                      R (*)() noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator!() noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_);
  }

  R operator!() const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_exclamation, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator!...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator+
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_plus,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator+(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator+(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_plus, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator+...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator-
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_minus,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator-(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator-(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_minus, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator-...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator/
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_slash,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator/(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator/(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_slash, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator/...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator%
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_percent,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator%(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator%(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_percent, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator%...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator^
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_caret,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator^(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator^(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_caret, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator^...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator&
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_ampersand,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator&(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator&(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_ampersand, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator&...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator|
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_pipe,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator|(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator|(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_pipe, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator|...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator+=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_plus_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator+=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator+=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_plus_equals, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator+=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator-=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_minus_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator-=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator-=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_minus_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator-=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator*=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_star_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator*=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator*=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_star_equals, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator*=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator/=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_slash_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator/=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator/=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_slash_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator/=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator%=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_percent_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator%=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator%=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_percent_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator%=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator^=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_caret_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator^=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator^=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_caret_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator^=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator&=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_ampersand_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator&=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator&=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_ampersand_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator&=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator|=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_pipe_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator|=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator|=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_pipe_equals, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator|=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator&&
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_ampersand_ampersand,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator&&(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator&&(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_ampersand_ampersand,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator&&...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator||
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_pipe_pipe,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator||(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator||(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_pipe_pipe, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator||...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator<<
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_less_less,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator<<(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator<<(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_less_less, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator<<...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator>>
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_greater_greater,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator>>(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator>>(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_greater_greater,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator>>...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator<<=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_less_less_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator<<=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator<<=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_less_less_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator<<=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator>>=
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_greater_greater_equals,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator>>=(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator>>=(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_greater_greater_equals,
                             ProtocolType, Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator>>=...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator++
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_plus_plus,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator++(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator++(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_plus_plus, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator++...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator--
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_minus_minus,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator--(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator--(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_minus_minus, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator--...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

// operator,
template <typename R, typename... Args, bool IsNoexcept, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst>
struct operator_thunk<std::meta::operators::op_comma,
                      R (*)(Args...) noexcept(IsNoexcept), ProtocolType, Vtable,
                      Member, IsConst> {
  R operator,(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

  R operator,(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->vtable_, protocol_object->object_,
        std::forward<Args>(args)...);
  }

 protected:
  operator_thunk() = default;
  ~operator_thunk() = default;
  operator_thunk(const operator_thunk&) = default;
  operator_thunk(operator_thunk&&) = default;
  operator_thunk& operator=(const operator_thunk&) = default;
  operator_thunk& operator=(operator_thunk&&) = default;
};

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_comma, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator, ...;

 protected:
  operator_overload_set() = default;
  ~operator_overload_set() = default;
  operator_overload_set(const operator_overload_set&) = default;
  operator_overload_set(operator_overload_set&&) = default;
  operator_overload_set& operator=(const operator_overload_set&) = default;
  operator_overload_set& operator=(operator_overload_set&&) = default;
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_OPERATOR_THUNKS_HH_

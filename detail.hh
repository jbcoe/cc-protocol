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
#ifndef XYZ_PROTOCOL_DETAIL_HH_
#define XYZ_PROTOCOL_DETAIL_HH_

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <meta>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "name_mangling.h"

// clang-p2996 deprecates data_member_options::no_unique_address in favour of
// a fork-specific attributes member that GCC does not have, so the warning is
// silenced for this file rather than moving off the standard field.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace xyz::detail {

template <std::meta::operators Operator = std::meta::operators::op_parentheses>
consteval bool is_operator(std::meta::info function) {
  return is_operator_function(function) && operator_of(function) == Operator;
}

// Returns `true` if `a` and `b` are the same operators or have the same
// identifier. Otherwise returns false.
consteval bool same_name(std::meta::info a, std::meta::info b) {
  if (has_identifier(a) && has_identifier(b)) {
    return identifier_of(a) == identifier_of(b);
  }
  if (is_operator_function(a) && is_operator_function(b)) {
    return operator_of(a) == operator_of(b);
  }
  return false;
}

// Returns `true` if the member functions `candidate` and `interface` have
// the same name, de-aliased return type and de-aliased parameter types.
consteval bool same_name_and_parameters(std::meta::info candidate,
                                        std::meta::info interface) {
  if (!same_name(candidate, interface)) return false;
  if (dealias(return_type_of(interface)) != dealias(return_type_of(candidate)))
    return false;
  auto dealiased_type_of = [](std::meta::info parameter) {
    return dealias(type_of(parameter));
  };
  return std::ranges::equal(parameters_of(interface), parameters_of(candidate),
                            {}, dealiased_type_of, dealiased_type_of);
}

// Returns `true` if the member functions `candidate` and `interface` have
// the same name, reference qualifiers, de-aliased return type and de-aliased
// parameter types; const and noexcept are not compared.
consteval bool same_signature_ignoring_const(std::meta::info candidate,
                                             std::meta::info interface) {
  if (is_lvalue_reference_qualified(interface) !=
      is_lvalue_reference_qualified(candidate))
    return false;
  if (is_rvalue_reference_qualified(interface) !=
      is_rvalue_reference_qualified(candidate))
    return false;
  return same_name_and_parameters(candidate, interface);
}

// Returns `true` if `candidate is a function with an explicit object parameter,
// the member functions `candidate` and `interface` have the
// same name, de-aliased return type and parameter types, and const / reference
// qualification.
consteval bool same_function_with_explicit_object(std::meta::info candidate,
                                                  std::meta::info interface) {
  if (!same_name(candidate, interface)) return false;
  if (dealias(return_type_of(candidate)) != dealias(return_type_of(interface)))
    return false;

  const auto params = parameters_of(candidate);
  if (params.empty() || !is_explicit_object_parameter(params.front()))
    return false;

  if (is_const(interface) !=
      is_const(remove_reference(type_of(params.front()))))
    return false;

  return std::ranges::equal(parameters_of(interface),
                            params | std::views::drop(1), {},
                            std::meta::type_of, std::meta::type_of);
}

// Returns `true` if the `candidate` member function is consistent with the
// `interface` member function for the purposes of structural subtyping;
// otherwise returns `false`.
consteval bool member_function_conforms_to(std::meta::info candidate,
                                           std::meta::info interface) {
  if (is_static_member(candidate)) {
    // A static candidate has no object parameter, so it satisfies any const
    // or reference qualification of `interface`.
    if (!same_name_and_parameters(candidate, interface)) return false;
  } else if (same_function_with_explicit_object(candidate, interface)) {
    // No additional conditions.
  } else {
    if (!same_signature_ignoring_const(candidate, interface)) return false;
    // `const` qualifiers must match.
    if (is_const(interface) != is_const(candidate)) return false;
  }
  // If interface is `noexcept`, `candidate` must be noexcept.
  return !is_noexcept(interface) || is_noexcept(candidate);
}

// Per ISO C++ ([expr.prim.lambda.closure]), closure types are unique, unnamed,
// non-union class types.
// The closure type is not an aggregate type.
// This concept will also match an unnamed class type with a single
// `operator()`.
//
// In practice a lambda has no base classes and no template arguments, although
// there is no wording in the standard to guarantee this.
//
// TODO(jbcoe): Refine this concept to match only lambdas.
template <typename T>
concept is_maybe_lambda =
    is_class_type(dealias(^^T)) && !has_identifier(dealias(^^T)) &&
    !is_aggregate_type(dealias(^^T)) && !has_template_arguments(dealias(^^T)) &&
    bases_of(dealias(^^T), std::meta::access_context::unprivileged()).empty() &&
    requires { &T::operator(); };

// The named, non-special member functions and call operators of `Type`,
// static or not, in declaration order.
template <std::meta::info Type>
consteval auto conformance_candidate_infos() {
  auto named =
      members_of(Type, std::meta::access_context::unprivileged()) |
      std::views::filter(std::meta::is_function) |
      std::views::filter([](std::meta::info member) consteval {
        return has_identifier(member) ||
               is_operator<std::meta::operators::op_parentheses>(member);
      });
  std::vector<std::meta::info> result(std::ranges::begin(named),
                                      std::ranges::end(named));
  // GCC Workaround: Per [meta.reflection.member.queries], a closure type's
  // function call operator is members-of-eligible, but GCC16's `members_of`
  // does not yet enumerate it, leaving `result` empty for lambdas; name the
  // operator directly as a fallback.
  using T = typename[:Type:];
  if constexpr (is_maybe_lambda<T>) {
    if (result.empty()) result.push_back(^^T::operator());
  }
  return result;
}

template <std::meta::info Type>
constexpr inline auto conformance_candidates_of =
    std::define_static_array(conformance_candidate_infos<Type>());

// The named, non-static, non-special member functions and call operators of
// `Type`, static or not, in declaration order. Ref-qualified functions are
// unsupported on protocol interfaces.
template <std::meta::info Type>
consteval std::vector<std::meta::info> protocol_interface_function_infos() {
  std::vector<std::meta::info> result;
  for (std::meta::info member : conformance_candidates_of<Type>) {
    if (is_static_member(member)) continue;
    if (is_lvalue_reference_qualified(member) ||
        is_rvalue_reference_qualified(member)) {
      std::string name = has_identifier(member)
                             ? std::string(identifier_of(member))
                             : "operator()";
      throw std::runtime_error("ref-qualified member function '" + name +
                               "' is not supported in a protocol interface");
    }
    result.push_back(member);
  }
  return result;
}

template <std::meta::info Type>
constexpr inline auto protocol_interface_functions_of =
    std::define_static_array(protocol_interface_function_infos<Type>());

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
    if constexpr (requires { protocol_object->valueless_after_move(); }) {
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
    if constexpr (requires { protocol_object->valueless_after_move(); }) {
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

template <typename R, typename... Args>
using fn_ptr_t = R (*)(Args...);

// One overload of a synthesised member function: the interface member (which
// names its vtable entry) and the const-qualification of the generated
// wrapper.
template <std::meta::info Member, bool IsConst>
struct overload_spec {};

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

// Thunk for one overload of a synthesised call operator. `operator()` can't
// be reached through a named member, so `operator_overload_set`
// derives from this thunk directly instead of holding it as a data member,
// letting `ProtocolType` be recovered with a plain static_cast.
template <std::meta::operators Operator, typename FnPtrType,
          typename ProtocolType, typename Vtable, std::meta::info Member,
          bool IsConst, bool IsNoexcept>
struct operator_thunk;

// TODO(jbcoe): Extend this approach to handle lvalue and rvalue qualifiers;
// until then `protocol_interface_function_infos` rejects ref-qualified
// interface members.
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
    if constexpr (requires { protocol_object->valueless_after_move(); }) {
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
    if constexpr (requires { protocol_object->valueless_after_move(); }) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    return vtable->[:vtable_entry:](protocol_object->object_,
                                    std::forward<Args>(args)...);
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
// `std::meta::operators` is not specified as it can be derived from
// OverloadSpec.
template <std::meta::operators Operator, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct operator_overload_set;

template <typename ProtocolType, typename Vtable, typename... OverloadSpecs>
struct operator_overload_set<std::meta::operators::op_parentheses, ProtocolType,
                             Vtable, OverloadSpecs...>
    : operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using operator_thunk_t<OverloadSpecs, ProtocolType, Vtable>::operator()...;
};

// How generated wrappers treat the const-qualification of interface members.
enum class const_policy {
  // `protocol<I>`: as declared in `I`, so `const protocol<I>` exposes only the
  // const member functions of `I` (const propagates).
  propagate,
  // `protocol_view<I>`: every wrapper is const-qualified regardless of `I`
  // (shallow const, as for `std::span`).
  all_const,
  // `protocol_view<const I>`: only the const member functions of `I`.
  const_only,
};

// Returns `true` if `member` if forwarded under `ConstPolicy`.
template <const_policy ConstPolicy>
consteval bool is_forwarded_member_function(
    std::meta::info member, std::span<const std::meta::info> members) {
  switch (ConstPolicy) {
    case const_policy::propagate:
      // `protocol<T>` propagates const through forwarded member function calls
      // so supports const and non-const qualified member functions.
      return true;
    case const_policy::const_only:
      // `protocol_view<const T>` forwards only const-qualified member
      // functions.
      return is_const(member);
    case const_policy::all_const:
      // `protocol_view<T>` is a view type: overload resolution through
      // a const and non-const access path must yield the same result.
      // A const-qualified member function is only given a forwarding wrapper if
      // no non-const-qualified with an otherwise identical signature exists.
      // (Aside: Oh the double negatives! If only `mutable` was the keyword.)
      if (!is_const(member)) {
        return true;
      } else {
        return std::ranges::none_of(members, [&](std::meta::info other) {
          return member != other &&
                 same_signature_ignoring_const(member, other);
        });
      }
  }
  std::unreachable();
}

// A single-member base wrapping the overload set for one interface member
// function name, named after that method (giving the
// `p.member_function_name(args)` call syntax).
template <std::meta::info Member, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct member_base_generator {
  struct type;
  consteval {
    // clang-format off
    std::meta::info thunk_type = substitute(
        ^^member_function_overload_set, {^^type, ^^ProtocolType, ^^Vtable, ^^OverloadSpecs...});

    define_aggregate(
      ^^type, {data_member_spec(thunk_type,
                             std::meta::data_member_options{
                              .name = identifier_of(Member),
                              .no_unique_address = true
                            })});
    // clang-format on
  }
};

template <std::meta::info Member, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
using member_base_t =
    member_base_generator<Member, ProtocolType, Vtable, OverloadSpecs...>::type;

// Combines the single-member base types and overload sets produced by
// `member_base_generator` and `X_operator_overload_set` into one type via
// multiple inheritance.
template <typename... MemberBases>
struct member_bases_wrapper : MemberBases... {};

// Returns a `member_bases_wrapper` specialisation with one base per public,
// non-special, member function name of `interface_type`, giving named members
// with an `operator()` for each overload selected by `ConstPolicy`, plus a
// `operator_overload_set` if `interface_type` has call operators.
// TODO: Rewrite this hard-to-read function.
template <std::meta::info InterfaceType, typename ProtocolType, typename Vtable,
          const_policy ConstPolicy>
consteval std::meta::info generate_member_bases_wrapper() {
  std::span<const std::meta::info> members =
      protocol_interface_functions_of<InterfaceType>;
  std::vector<std::meta::info> member_base_types;
  std::vector<std::meta::info> matched_members;
  for (std::meta::info member : members) {
    // Find unique names/operators.
    if (std::ranges::any_of(matched_members,
                            [&](std::meta::info matched_member) {
                              return same_name(member, matched_member);
                            }))
      continue;
    matched_members.push_back(member);

    // Collect overloads.
    std::vector<std::meta::info> overload_specs;
    for (std::meta::info overload : members) {
      if (!same_name(overload, member) ||
          !is_forwarded_member_function<ConstPolicy>(overload, members))
        continue;
      const bool wrapper_is_const =
          ConstPolicy == const_policy::propagate ? is_const(overload) : true;
      // clang-format off
      overload_specs.push_back(substitute(
          ^^overload_spec, {reflect_constant(overload),
                            std::meta::reflect_constant(wrapper_is_const)}));
      // clang-format on
    }
    if (overload_specs.empty()) continue;

    std::vector<std::meta::info> member_base_args;
    if (has_identifier(member)) {
      member_base_args.push_back(reflect_constant(member));
    } else if (is_operator<std::meta::operators::op_parentheses>(member)) {
      member_base_args.push_back(
          reflect_constant(std::meta::operators::op_parentheses));
    }
    member_base_args.push_back(^^ProtocolType);
    member_base_args.push_back(^^Vtable);
    member_base_args.append_range(overload_specs);
    if (has_identifier(member)) {
      member_base_types.push_back(
          substitute(^^member_base_t, member_base_args));
    } else if (is_operator<std::meta::operators::op_parentheses>(member)) {
      member_base_types.push_back(
          substitute(^^operator_overload_set, member_base_args));
    } else {
      std::unreachable();
    }
  }
  return substitute(^^member_bases_wrapper, member_base_types);
}

// The generated wrapper type for `T`: a `member_bases_wrapper` specialisation
// with named members with `operator()` for each public, non-special, member
// function from `T` selected by `ConstPolicy`.
template <typename T, typename ProtocolType, typename Vtable,
          const_policy ConstPolicy>
using protocol_wrappers_t =
    typename[:generate_member_bases_wrapper<^^T, ProtocolType, Vtable,
                                            ConstPolicy>():];

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

// Finds the member of `CandidateType` that structurally conforms to
// `Member`.
template <std::meta::info Member, std::meta::info CandidateType>
consteval std::meta::info find_conforming_member() {
  for (std::meta::info candidate : conformance_candidates_of<CandidateType>) {
    if (member_function_conforms_to(candidate, Member)) return candidate;
  }
  std::unreachable();
}

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

#pragma GCC diagnostic pop
#endif  // XYZ_PROTOCOL_DETAIL_HH_

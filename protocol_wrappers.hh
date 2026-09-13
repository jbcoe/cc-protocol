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
#ifndef XYZ_PROTOCOL_PROTOCOL_WRAPPERS_HH_
#define XYZ_PROTOCOL_PROTOCOL_WRAPPERS_HH_

#include <algorithm>
#include <meta>
#include <span>
#include <vector>

#include "conformance.hh"
#include "member_function_thunks.hh"
#include "operator_thunks.hh"
#include "overload_spec.hh"

// clang-p2996 deprecates data_member_options::no_unique_address in favour of
// a fork-specific attributes member that GCC does not have, so the warning is
// silenced for this file rather than moving off the standard field.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace xyz::detail {

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
    } else if (is_operator<std::meta::operators::op_square_brackets>(member)) {
      member_base_args.push_back(
          reflect_constant(std::meta::operators::op_square_brackets));
    } else if (is_operator<std::meta::operators::op_star>(member)) {
      member_base_args.push_back(
          reflect_constant(std::meta::operators::op_star));
    } else if (is_operator<std::meta::operators::op_arrow>(member)) {
      member_base_args.push_back(
          reflect_constant(std::meta::operators::op_arrow));
    }
    member_base_args.push_back(^^ProtocolType);
    member_base_args.push_back(^^Vtable);
    member_base_args.append_range(overload_specs);
    if (has_identifier(member)) {
      member_base_types.push_back(
          substitute(^^member_base_t, member_base_args));
    } else if (is_operator_function(member)) {
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

}  // namespace xyz::detail

#pragma GCC diagnostic pop
#endif  // XYZ_PROTOCOL_PROTOCOL_WRAPPERS_HH_

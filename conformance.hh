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
#ifndef XYZ_PROTOCOL_CONFORMANCE_HH_
#define XYZ_PROTOCOL_CONFORMANCE_HH_

#include <algorithm>
#include <meta>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace xyz::detail {

template <std::meta::operators Operator>
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
               is_operator<std::meta::operators::op_star>(member) ||
               is_operator<std::meta::operators::op_arrow>(member) ||
               is_operator<std::meta::operators::op_parentheses>(member) ||
               is_operator<std::meta::operators::op_square_brackets>(member);
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

// The number of parameters `member` declares beyond its object parameter:
// `parameters_of` includes an explicit object parameter (from "deducing
// this"), so that one is excluded to count only the operands a caller
// supplies.
consteval size_t declared_argument_count(std::meta::info member) {
  auto params = parameters_of(member);
  if (!params.empty() && is_explicit_object_parameter(params.front())) {
    return params.size() - 1;
  }
  return params.size();
}

// The named, non-static, non-special member functions and call operators of
// `Type`, static or not, in declaration order. Ref-qualified functions and
// binary `operator*` are unsupported on protocol interfaces.
template <std::meta::info Type>
consteval std::vector<std::meta::info> protocol_interface_function_infos() {
  std::vector<std::meta::info> result;
  for (std::meta::info member : conformance_candidates_of<Type>) {
    if (is_static_member(member)) continue;
    if (is_lvalue_reference_qualified(member) ||
        is_rvalue_reference_qualified(member)) {
      std::string name = has_identifier(member)
                             ? std::string(identifier_of(member))
                             : std::string(display_string_of(member));
      throw std::runtime_error("ref-qualified member function '" + name +
                               "' is not supported in a protocol interface");
    }
    if (is_operator<std::meta::operators::op_star>(member) &&
        declared_argument_count(member) != 0) {
      throw std::runtime_error(
          "binary operator* is not supported in a protocol interface");
    }
    result.push_back(member);
  }
  return result;
}

template <std::meta::info Type>
constexpr inline auto protocol_interface_functions_of =
    std::define_static_array(protocol_interface_function_infos<Type>());

// Finds the member of `CandidateType` that structurally conforms to
// `Member`.
template <std::meta::info Member, std::meta::info CandidateType>
consteval std::meta::info find_conforming_member() {
  for (std::meta::info candidate : conformance_candidates_of<CandidateType>) {
    if (member_function_conforms_to(candidate, Member)) return candidate;
  }
  std::unreachable();
}

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_CONFORMANCE_HH_

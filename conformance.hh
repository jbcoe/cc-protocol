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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xyz::detail {

// Returns `true` if `a` and `b` are the same operators, the same conversion
// function, or have the same identifier. Otherwise returns false. A
// conversion function's name is its (dealiased) target type.
consteval bool same_name(std::meta::info a, std::meta::info b) {
  if (has_identifier(a) && has_identifier(b)) {
    return identifier_of(a) == identifier_of(b);
  }
  if (is_operator_function(a) && is_operator_function(b)) {
    return operator_of(a) == operator_of(b);
  }
  if (is_conversion_function(a) && is_conversion_function(b)) {
    return dealias(return_type_of(a)) == dealias(return_type_of(b));
  }
  return false;
}

// Returns `true` if the member functions `candidate` and `interface` have
// the same name and de-aliased parameter types.
consteval bool same_name_and_parameters(std::meta::info candidate,
                                        std::meta::info interface) {
  if (!same_name(candidate, interface)) return false;
  auto dealiased_type_of = [](std::meta::info parameter) {
    return type_of(parameter);
  };
  return std::ranges::equal(parameters_of(interface), parameters_of(candidate),
                            {}, dealiased_type_of, dealiased_type_of);
}

// Returns `true` if the member functions `candidate` and `interface` have
// the same name, de-aliased return type and de-aliased parameter types.
consteval bool same_name_return_type_and_parameters(std::meta::info candidate,
                                                    std::meta::info interface) {
  if (!same_name(candidate, interface)) return false;
  if (dealias(return_type_of(interface)) != dealias(return_type_of(candidate)))
    return false;
  auto dealiased_type_of = [](std::meta::info parameter) {
    return type_of(parameter);
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
  return same_name_return_type_and_parameters(candidate, interface);
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
    if (!same_name_return_type_and_parameters(candidate, interface))
      return false;
  } else if (same_function_with_explicit_object(candidate, interface)) {
    // No additional conditions.
  } else {
    if (!same_signature_ignoring_const(candidate, interface)) return false;
    // `const` qualifiers must match.
    if (is_const(interface) != is_const(candidate)) return false;
  }
  // If interface is `noexcept`, `candidate` must be noexcept.
  if (is_noexcept(interface) && !is_noexcept(candidate)) return false;

  // `explicit` qualifiers must match.
  return is_explicit(interface) == is_explicit(candidate);
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

// The named, non-special member functions, operators and conversion
// functions of `type`, static or not, in declaration order. Not a template,
// so the range adaptors and the closure type are instantiated once rather
// than once per type.
consteval std::vector<std::meta::info> named_member_function_infos(
    std::meta::info type) {
  auto named =
      members_of(type, std::meta::access_context::unprivileged()) |
      std::views::filter(std::meta::is_function) |
      std::views::filter([](std::meta::info member) consteval {
        return !is_special_member_function(member) &&
               (has_identifier(member) || is_operator_function(member) ||
                is_conversion_function(member));
      });
  return std::vector<std::meta::info>(std::ranges::begin(named),
                                      std::ranges::end(named));
}

// The named, non-special member functions, operators and conversion
// functions of `Type`, static or not, in declaration order.
template <std::meta::info Type>
consteval auto conformance_candidate_infos() {
  std::vector<std::meta::info> result = named_member_function_infos(Type);
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

// Throws to reject `member` of a protocol interface; `kind` says what is
// unsupported about it. The message names `member` by its identifier if it
// has one and by its display string otherwise, which for an operator or a
// conversion function names the exact overload.
[[noreturn]] consteval void reject_interface_member(std::string_view kind,
                                                    std::meta::info member) {
  std::string name = has_identifier(member)
                         ? std::string(identifier_of(member))
                         : std::string(display_string_of(member));
  throw std::runtime_error(std::string(kind) + " '" + name +
                           "' is not supported in a protocol interface");
}

// Some operators are excluded from protocol interfaces.
consteval bool is_unsupported_operator(std::meta::operators op) {
  using enum std::meta::operators;
  return op == op_equals || op == op_co_await;
}

// `true` for `==`, `!=`, `<`, `<=`, `>`, `>=` and `<=>`.
consteval bool is_comparison_operator(std::meta::operators op) {
  using enum std::meta::operators;
  return op == op_equals_equals || op == op_exclamation_equals ||
         op == op_less || op == op_less_equals || op == op_greater ||
         op == op_greater_equals || op == op_spaceship;
}

// Throws if `type` has a non-static member function template, operator
// template or conversion function template. A template cannot be forwarded
// through a vtable, and `named_member_function_infos` does not return one, so
// without this check an interface would silently lose the member.
// Constructor templates and static member function templates are ignored, as
// constructors and static member functions are. Not a template, for the
// reason given on `named_member_function_infos`.
consteval void reject_member_function_templates(std::meta::info type) {
  for (std::meta::info member :
       members_of(type, std::meta::access_context::unprivileged())) {
    if (!is_function_template(member) || is_constructor_template(member) ||
        is_static_member(member)) {
      continue;
    }
    reject_interface_member("member function template", member);
  }
}

// Throws if `member`, a conversion function named by
// `reject_placeholder_conversion_functions`, is one `members_of` would have
// returned had its return type been deduced. A compiler may also find a
// function with a body by this name; `members_of` returns that one, so it is
// left alone.
consteval void reject_placeholder_conversion_function(std::meta::info member) {
  std::meta::access_context context = std::meta::access_context::unprivileged();
  if (!is_accessible(member, context) ||
      std::ranges::contains(members_of(parent_of(member), context), member)) {
    return;
  }
  reject_interface_member("conversion function with a placeholder return type",
                          member);
}

// Throws if `T` declares `operator auto()` or `operator decltype(auto)()`
// without a body. The return type of such a function is never deduced, so
// `members_of` does not return it ([meta.reflection.member.queries]) and
// without this check an interface would silently lose the member. The
// function can only be found by naming it, so any other member function
// declared with a placeholder return type and no body, such as
// `auto f() const;` or `operator const auto&() const;`, is still lost. One
// with a body has its return type deduced and is an ordinary member.
template <typename T>
consteval void reject_placeholder_conversion_functions() {
  if constexpr (requires { ^^T::operator auto; }) {
    reject_placeholder_conversion_function(^^T::operator auto);
  }
  if constexpr (requires { ^^T::operator decltype(auto); }) {
    reject_placeholder_conversion_function(^^T::operator decltype(auto));
  }
}

// The named, non-static, non-special member functions, operators and
// conversion functions of `Type`, in declaration order.
// Ref-qualified functions, explicit-object member functions, member function
// templates, conversion functions to `auto` or `decltype(auto)`, defaulted
// comparison operators, comparisons with `Type` and the operators
// `is_unsupported_operator` names are unsupported on protocol interfaces.
template <std::meta::info Type>
consteval std::vector<std::meta::info> protocol_interface_function_infos() {
  reject_member_function_templates(Type);
  reject_placeholder_conversion_functions<typename[:Type:]>();
  std::vector<std::meta::info> result;
  for (std::meta::info member : conformance_candidates_of<Type>) {
    if (is_static_member(member)) continue;
    if (is_lvalue_reference_qualified(member) ||
        is_rvalue_reference_qualified(member)) {
      reject_interface_member("ref-qualified member function", member);
    }
    std::vector<std::meta::info> params = parameters_of(member);
    if (!params.empty() && is_explicit_object_parameter(params.front())) {
      reject_interface_member("explicit-object member function", member);
    }
    if (is_operator_function(member)) {
      std::meta::operators op = operator_of(member);
      if (op == std::meta::operators::op_ampersand && params.empty()) {
        reject_interface_member("address-of operator", member);
      }
      // Special member functions are filtered out above, so a defaulted
      // operator is a comparison. Forwarding it would require a candidate to
      // compare itself with `Type`, which is not what `= default` asks for.
      if (is_defaulted(member)) {
        reject_interface_member("defaulted comparison operator", member);
      }
      // A comparison with `Type` itself is reserved: forwarding it would
      // require a candidate to compare itself with `Type`, and rejecting it
      // leaves room to make it compare two protocols later.
      if (is_comparison_operator(op) &&
          std::ranges::any_of(params, [](std::meta::info parameter) {
            return dealias(remove_cvref(type_of(parameter))) == dealias(Type);
          })) {
        reject_interface_member("comparison with the interface type", member);
      }
      if (is_unsupported_operator(op)) {
        reject_interface_member("operator", member);
      }
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

// Returns `true` if every interface member has a conforming candidate. This
// and `contains_same_name` are not templates, so their closure types and the
// algorithms over them are instantiated once rather than once per interface.
consteval bool all_members_conform(
    std::span<const std::meta::info> interface_members,
    std::span<const std::meta::info> candidate_members) {
  return std::ranges::all_of(
      interface_members, [&](std::meta::info interface_member) {
        return std::ranges::any_of(candidate_members,
                                   [&](std::meta::info candidate_member) {
                                     return member_function_conforms_to(
                                         candidate_member, interface_member);
                                   });
      });
}

// Returns `true` if `members` has a member with the same name as `member`.
consteval bool contains_same_name(std::span<const std::meta::info> members,
                                  std::meta::info member) {
  return std::ranges::any_of(
      members, [&](std::meta::info other) { return same_name(member, other); });
}

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_CONFORMANCE_HH_

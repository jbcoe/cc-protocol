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
#ifndef XYZ_REFLECTION_PROTOCOL_H_
#define XYZ_REFLECTION_PROTOCOL_H_

// A C++26-reflection-based implementation of protocol and protocol_view.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <meta>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace xyz::reflection {

template <typename T, typename Allocator>
class protocol;

template <typename T>
class protocol_view;

template <typename T>
struct is_protocol : std::false_type {};

template <typename T, typename Allocator>
struct is_protocol<protocol<T, Allocator>> : std::true_type {};

template <typename T>
inline constexpr bool is_protocol_v = is_protocol<T>::value;

template <typename T>
struct is_protocol_view : std::false_type {};

template <typename T>
struct is_protocol_view<protocol_view<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_protocol_view_v = is_protocol_view<T>::value;

namespace detail {

// Returns `true` if the `candidate` member function is consistent with the
// `interface` member function for the purposes of structural subtyping;
// otherwise returns `false`.
consteval bool member_function_signatures_match(std::meta::info interface,
                                                std::meta::info candidate) {
  if (!has_identifier(interface) || !has_identifier(candidate)) return false;
  if (identifier_of(interface) != identifier_of(candidate)) return false;
  // If interface is `const`, `candidate` must be const.
  if (is_const(interface) != is_const(candidate)) return false;
  // If interface is `noexcept`, `candidate` must be noexcept.
  if (is_noexcept(interface) && !is_noexcept(candidate)) return false;
  // Reference qualifiers must match.
  if (is_lvalue_reference_qualified(interface) !=
      is_lvalue_reference_qualified(candidate))
    return false;
  if (is_rvalue_reference_qualified(interface) !=
      is_rvalue_reference_qualified(candidate))
    return false;
  // De-aliased return types must match.
  if (dealias(return_type_of(interface)) != dealias(return_type_of(candidate)))
    return false;
  std::vector<std::meta::info> interface_params = parameters_of(interface);
  std::vector<std::meta::info> candidate_params = parameters_of(candidate);
  // parameter counts must match.
  if (interface_params.size() != candidate_params.size()) return false;
  for (std::size_t i = 0; i < interface_params.size(); ++i) {
    // De-aliased parameter types must match.
    if (dealias(type_of(interface_params[i])) !=
        dealias(type_of(candidate_params[i])))
      return false;
  }
  return true;
}

}  // namespace detail

// Returns `true` if `Candidate` is a structural subtype of `Interface`;
// otherwise returns `false`.
template <typename Interface, typename Candidate>
consteval bool is_protocol_conformant() {
  static_assert(std::is_same_v<Interface, std::remove_cvref_t<Interface>>,
                "Interface must not be cv/ref-qualified: strip qualifiers at "
                "the call site with std::remove_cvref_t.");
  static_assert(std::is_same_v<Candidate, std::remove_cvref_t<Candidate>>,
                "Candidate must not be cv/ref-qualified: strip qualifiers at "
                "the call site with std::remove_cvref_t.");

  // O(N*M) over member counts, assumed to be negligible at compile time.
  std::ranges::range auto interface_member_functions =
      std::define_static_array(
          members_of(^^Interface, std::meta::access_context::unprivileged())) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::meta::has_identifier);

  std::ranges::range auto candidate_member_functions =
      std::define_static_array(
          members_of(^^Candidate, std::meta::access_context::unprivileged())) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::meta::has_identifier);

  return std::ranges::all_of(
      interface_member_functions, [&](std::meta::info interface_member) {
        return std::ranges::any_of(
            candidate_member_functions, [&](std::meta::info candidate_member) {
              return detail::member_function_signatures_match(interface_member,
                                                              candidate_member);
            });
      });
}

// Variable template for use in requires clauses.
template <typename Interface, typename Candidate>
inline constexpr bool is_protocol_conformant_v =
    is_protocol_conformant<Interface, Candidate>();

template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol {
 public:
  protocol() = delete;  // Deleted as `T` is used as an interface type.

  protocol(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol(protocol&&);  // Unconstrained

  protocol& operator=(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol& operator=(protocol&&);  // Unconstrained.

  ~protocol();  // Unconstrained.

  // Construct from any type U that conforms to the Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_v<std::remove_cvref_t<U>>)
  explicit protocol(U&& value);

  // Construct a U in place from Ts..., where U conforms to the Interface T.
  template <typename U, typename... Ts>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_v<std::remove_cvref_t<U>>)
  explicit protocol(std::in_place_type_t<U>, Ts&&... ts);
};

template <typename T>
class protocol_view {
 public:
  // The default construtor is deleted as a default constructed protocol_view
  // would be empty.
  protocol_view() = delete;

  // Remaining special member functions are defaulted.
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) noexcept = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) noexcept = default;
  ~protocol_view() = default;

  // Construct from any type U that conforms to the Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_view_v<std::remove_cvref_t<U>>)
  explicit protocol_view(const U& object);
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_H_

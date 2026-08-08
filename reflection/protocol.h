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
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/
#ifndef XYZ_REFLECTION_PROTOCOL_H_
#define XYZ_REFLECTION_PROTOCOL_H_

// A C++26-reflection-based implementation of protocol and protocol_view.

#include <cstddef>
#include <memory>
#include <meta>
#include <type_traits>
#include <vector>

namespace xyz::reflection {

namespace detail {

// Returns true if both member functions share the same name, return type,
// parameter types, and constness.
consteval bool member_function_signatures_match(std::meta::info lhs,
                                                std::meta::info rhs) {
  if (!has_identifier(lhs) || !has_identifier(rhs)) return false;
  if (identifier_of(lhs) != identifier_of(rhs)) return false;
  if (is_const(lhs) != is_const(rhs)) return false;
  if (dealias(return_type_of(lhs)) != dealias(return_type_of(rhs)))
    return false;
  std::vector<std::meta::info> lhs_params = parameters_of(lhs);
  std::vector<std::meta::info> rhs_params = parameters_of(rhs);
  if (lhs_params.size() != rhs_params.size()) return false;
  for (std::size_t i = 0; i < lhs_params.size(); ++i) {
    if (dealias(type_of(lhs_params[i])) != dealias(type_of(rhs_params[i])))
      return false;
  }
  return true;
}

}  // namespace detail

// Returns true at compile time if every public member function declared in
// Interface is present in Concrete with a matching signature (name, return
// type, parameter types, and constness).
template <typename Interface, typename Concrete>
consteval bool conforms_to() {
  for (std::meta::info interface_member :
       members_of(^^Interface, std::meta::access_context::unprivileged())) {
    if (!is_member_function(interface_member)) continue;
    bool found = false;
    for (std::meta::info concrete_member :
         members_of(^^Concrete, std::meta::access_context::unprivileged())) {
      if (!is_member_function(concrete_member)) continue;
      if (detail::member_function_signatures_match(interface_member,
                                                   concrete_member)) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

// Variable template for use in requires clauses.
template <typename Interface, typename Concrete>
inline constexpr bool conforms_to_v = conforms_to<Interface, Concrete>();

template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol {
 public:
  // Special member functions.
  protocol() = delete;

  protocol(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol(protocol&&)
    requires std::is_move_constructible_v<T>;

  protocol& operator=(const protocol&)
    requires std::is_copy_assignable_v<T>;

  protocol& operator=(protocol&&)
    requires std::is_move_assignable_v<T>;

  ~protocol();  // Unconstrained.

  // Construct from any Concrete type that conforms to the Interface T.
  template <typename Concrete>
    requires conforms_to_v<T, std::remove_cvref_t<Concrete>>
  explicit protocol(Concrete&& value);
};

template <typename T>
class protocol_view {
 public:
  // Special member functions.
  protocol_view() = delete;
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) = default;
  ~protocol_view() = default;

  // Construct from any Concrete type that conforms to the Interface T.
  template <typename Concrete>
    requires conforms_to_v<T, std::remove_cvref_t<Concrete>>
  explicit protocol_view(const Concrete& object);
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_H_

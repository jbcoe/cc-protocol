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
#ifndef XYZ_REFLECTION_PROTOCOL_HH_
#define XYZ_REFLECTION_PROTOCOL_HH_

// A C++26-reflection-based implementation of protocol and protocol_view.
//
// Member function stubs are synthesised at compile time for every public
// non-special member function declared in the Interface type.
//
// The stubs are currently unimplemented beyond providing member function
// signatures.

#include <algorithm>
#include <cstddef>
#include <functional>
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
consteval bool member_function_conforms_to(std::meta::info candidate,
                                           std::meta::info interface) {
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

// The named, non-static, non-special member functions of `Type`.
template <std::meta::info Type>
consteval auto protocol_interface_functions_of() {
  return std::define_static_array(
      std::define_static_array(
          members_of(Type, std::meta::access_context::unprivileged())) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::not_fn(std::meta::is_static_member)) |
      std::views::filter(std::meta::has_identifier));
}

// ---------------------------------------------------------------------------
// Vanishing-this-pointer thunk for a synthesised member stub.
//
// The thunk carries a single operator() whose signature mirrors one method
// of the Interface type.
// ---------------------------------------------------------------------------
template <typename FnPtrType, typename EnclosingType, bool IsConst,
          bool IsNoexcept>
struct method_thunk;

template <typename R, typename... Args, typename EnclosingType, bool IsConst,
          bool IsNoexcept>
struct method_thunk<R (*)(Args...), EnclosingType, IsConst, IsNoexcept> {
  R operator()(Args... /*args*/) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    [[maybe_unused]] auto* base =
        static_cast<EnclosingType*>(static_cast<void*>(this));
    std::unreachable();  // vtable dispatch not yet implemented
  }

  R operator()(Args... /*args*/) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    [[maybe_unused]] const auto* base =
        static_cast<const EnclosingType*>(static_cast<const void*>(this));
    std::unreachable();  // vtable dispatch not yet implemented
  }
};

// A convenient alias for function pointers.
template <typename R, typename... Args>
using fn_ptr_t = R (*)(Args...);

// ---------------------------------------------------------------------------
// Returns a list of data_member_spec values, one for each member function
// implemented by `protocol`.
//
// Each data_member_spec names the data member after the interface method
// (giving the `p.method_name(args)` call syntax) and sets its type to the
// matching thunk template specialisation.
//
// Because C++ disallows two data members with the same name inside the same
// class, overloaded methods will cause compile-time errors.
// We will address this limitation in a follow-up PR.
// ---------------------------------------------------------------------------
consteval std::vector<std::meta::info> generate_method_thunk_specs(
    std::meta::info interface_type, std::meta::info enclosing_type) {
  std::vector<std::meta::info> method_thunk_specs;

  std::ranges::range auto members =
      std::define_static_array(members_of(
          interface_type, std::meta::access_context::unprivileged())) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::not_fn(std::meta::is_special_member_function)) |
      std::views::filter(std::not_fn(std::meta::is_static_member)) |
      std::views::filter(std::meta::has_identifier);

  for (std::meta::info member : members) {
    std::string_view name = identifier_of(member);

    // Build the function-pointer type R(*)(Args...) from the method's
    // return type and parameter types.
    std::vector<std::meta::info> fn_args{dealias(return_type_of(member))};
    std::vector<std::meta::info> member_parameters = parameters_of(member);
    for (std::meta::info parameter : member_parameters) {
      fn_args.push_back(dealias(type_of(parameter)));
    }
    std::meta::info fn_ptr_type = substitute(^^fn_ptr_t, fn_args);

    std::meta::info thunk_type = substitute(
        ^^method_thunk, {
                            fn_ptr_type, enclosing_type,
                            std::meta::reflect_constant(is_const(member)),
                            std::meta::reflect_constant(is_noexcept(member))});

    method_thunk_specs.push_back(data_member_spec(
        thunk_type, std::meta::data_member_options{.name = name,
                                                   .no_unique_address = true}));
  }
  return method_thunk_specs;
}

// Generates a struct that has named members with `operator()` for each public,
// non-special, member function from `T`.
template <typename T>
struct protocol_member_stubs_generator {
  struct stubs;
  consteval {
    define_aggregate(^^stubs, generate_method_thunk_specs(^^T, ^^stubs));
  }
};

}  // namespace detail

// Returns `true` if `U` is a structural subtype of the interface `T`;
// otherwise returns `false`.
template <typename T, typename U>
consteval bool is_protocol_conformant() {
  static_assert(std::is_same_v<T, std::remove_cvref_t<T>>,
                "the interface type must not be cv/ref-qualified: strip "
                "qualifiers at the call site with std::remove_cvref_t.");
  static_assert(std::is_same_v<U, std::remove_cvref_t<U>>,
                "the candidate type must not be cv/ref-qualified: strip "
                "qualifiers at the call site with std::remove_cvref_t.");

  // Checking for protocol interface conformance is O(N*M) over member counts,
  // assumed to be negligible at compile time.
  auto interface_member_functions =
      detail::protocol_interface_functions_of<^^T>();
  auto candidate_member_functions =
      detail::protocol_interface_functions_of<^^U>();

  return std::ranges::all_of(
      interface_member_functions, [&](std::meta::info interface_member) {
        return std::ranges::any_of(candidate_member_functions,
                                   [&](std::meta::info candidate_member) {
                                     return detail::member_function_conforms_to(
                                         candidate_member, interface_member);
                                   });
      });
}

// Variable template for use in requires clauses.
template <typename T, typename U>
inline constexpr bool is_protocol_conformant_v = is_protocol_conformant<T, U>();

// ---------------------------------------------------------------------------
// protocol<T, Allocator>
// ---------------------------------------------------------------------------
template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol : public detail::protocol_member_stubs_generator<T>::stubs {
 public:
  protocol() = delete;  // Deleted as `T` is used as an interface type.

  protocol(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol(protocol&&);  // Unconstrained

  protocol& operator=(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol& operator=(protocol&&);  // Unconstrained.

  ~protocol();  // Unconstrained.

  // Construct from any type `U` that conforms to the interface `T`.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_v<std::remove_cvref_t<U>>)
  explicit protocol(U&& value);

  // Construct a `U` in place from `Ts...`, where `U` conforms to the interface
  // `T`.
  template <typename U, typename... Ts>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_v<std::remove_cvref_t<U>>)
  explicit protocol(std::in_place_type_t<U>, Ts&&... ts);
};

// ---------------------------------------------------------------------------
// protocol_view<T>
// ---------------------------------------------------------------------------
template <typename T>
class protocol_view : public detail::protocol_member_stubs_generator<T>::stubs {
 public:
  // The default construtor is deleted as a default constructed `protocol_view`
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
#endif  // XYZ_REFLECTION_PROTOCOL_HH_

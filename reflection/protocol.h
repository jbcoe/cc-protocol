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
//
// Member function stubs are synthesised at compile time for every public
// non-special member function declared in the Interface type.  The stubs
// are attached to protocol and protocol_view through data members that
// provide ordinary member-function call syntax via the "vanishing this
// pointer" technique described in tutorials/2_vanishing_this_pointer.cc
// (section 5): each per-method wrapper sits as the sole member of a
// dedicated base struct; the wrapper's operator() recovers the enclosing
// base address through a static_cast and hands it to the derived class.
//
// The stubs are intentionally unimplemented beyond the signature: they
// call std::unreachable() so that the type-system plumbing can be
// developed before the vtable dispatch layer exists.

#include <cstddef>
#include <memory>
#include <meta>
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
struct is_protocol_view : std::false_type {};

template <typename T>
struct is_protocol_view<protocol_view<T>> : std::true_type {};

template <typename T>
concept is_neither_protocol_nor_protocol_view =
    !is_protocol<std::remove_cvref_t<T>>::value &&
    !is_protocol_view<std::remove_cvref_t<T>>::value;

namespace detail {

// Returns true if the concrete member function (rhs) satisfies the interface
// member function (lhs) with respect to: name, return type, parameter types,
// constness, ref-qualifier, and noexcept. For noexcept, the rule is:
// - If the interface requires noexcept, the concrete must also be noexcept.
// - If the interface does not require noexcept, the concrete may be either
//   (a noexcept concrete is still conformant with a non-noexcept interface).
// Ref-qualifiers (none, &, &&) must match exactly.
consteval bool member_function_signatures_match(std::meta::info lhs,
                                                std::meta::info rhs) {
  if (!has_identifier(lhs) || !has_identifier(rhs)) return false;
  if (identifier_of(lhs) != identifier_of(rhs)) return false;
  if (is_const(lhs) != is_const(rhs)) return false;
  if (is_lvalue_reference_qualified(lhs) != is_lvalue_reference_qualified(rhs))
    return false;
  if (is_rvalue_reference_qualified(lhs) != is_rvalue_reference_qualified(rhs))
    return false;
  if (is_noexcept(lhs) && !is_noexcept(rhs)) return false;
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

// ---------------------------------------------------------------------------
// Vanishing-this-pointer thunks for synthesised member stubs.
//
// Each thunk type carries a single operator() whose signature mirrors one
// method of the Interface type.  The thunk lives as a [[no_unique_address]]
// data member inside a per-method base struct (section 5 of the tutorial);
// operator() recovers the base struct's address from its own address (valid
// because the thunk is the sole, offset-zero member of that base) and then
// casts further to the protocol or protocol_view that inherits from it.
//
// The four partial specialisations below cover the four combinations of
// const/non-const × noexcept/potentially-throwing that an interface method
// can declare.
// ---------------------------------------------------------------------------

// Helper alias used by method_thunk_specs to build function-pointer types
// that encode the method's return type and parameter types.
template <typename R, typename... Args>
using fn_ptr_t = R (*)(Args...);

// Non-const, potentially-throwing method thunk.
// OwnerBase is the dedicated base struct holding this thunk; the derived
// class (protocol or protocol_view) inherits from OwnerBase.
template <typename FnPtrType, typename OwnerBase>
struct method_thunk_mutable;

template <typename R, typename... Args, typename OwnerBase>
struct method_thunk_mutable<R (*)(Args...), OwnerBase> {
  // Provides member-function call syntax.  Recovers the OwnerBase pointer
  // through the vanishing-this-pointer cast, then calls the protocol's
  // stored vtable entry (not yet implemented: stub calls std::unreachable).
  R operator()(Args... /*args*/) {
    // Vanishing this pointer: this thunk is at offset 0 of OwnerBase.
    [[maybe_unused]] auto* base =
        static_cast<OwnerBase*>(static_cast<void*>(this));
    std::unreachable();  // stub — vtable dispatch not yet implemented
  }
};

// Const, potentially-throwing method thunk.
template <typename FnPtrType, typename OwnerBase>
struct method_thunk_const;

template <typename R, typename... Args, typename OwnerBase>
struct method_thunk_const<R (*)(Args...), OwnerBase> {
  R operator()(Args... /*args*/) const {
    [[maybe_unused]] const auto* base =
        static_cast<const OwnerBase*>(static_cast<const void*>(this));
    std::unreachable();  // stub — vtable dispatch not yet implemented
  }
};

// Non-const, noexcept method thunk.
template <typename FnPtrType, typename OwnerBase>
struct method_thunk_mutable_noexcept;

template <typename R, typename... Args, typename OwnerBase>
struct method_thunk_mutable_noexcept<R (*)(Args...), OwnerBase> {
  R operator()(Args... /*args*/) noexcept {
    [[maybe_unused]] auto* base =
        static_cast<OwnerBase*>(static_cast<void*>(this));
    std::unreachable();  // stub — vtable dispatch not yet implemented
  }
};

// Const, noexcept method thunk.
template <typename FnPtrType, typename OwnerBase>
struct method_thunk_const_noexcept;

template <typename R, typename... Args, typename OwnerBase>
struct method_thunk_const_noexcept<R (*)(Args...), OwnerBase> {
  R operator()(Args... /*args*/) const noexcept {
    [[maybe_unused]] const auto* base =
        static_cast<const OwnerBase*>(static_cast<const void*>(this));
    std::unreachable();  // stub — vtable dispatch not yet implemented
  }
};

// ---------------------------------------------------------------------------
// Compile-time helper: build a list of data_member_spec values, one per
// public non-special member function of interface_type.
//
// Each spec names the data member after the interface method (giving the
// p.method_name(args) call syntax) and sets its type to the matching thunk
// template specialisation.  [[no_unique_address]] is requested so that
// thunks for methods returning void add no storage overhead.
//
// Methods that share a name (overloads) are each given their own data
// member.  Because C++ disallows two data members with the same name inside
// the same class, overloaded methods whose names collide are skipped after
// the first occurrence and excluded from the synthesised bases.  A future
// revision can handle overloads by encoding the parameter types into the
// member name (see tutorials/3_reflection.cc section 5).
// ---------------------------------------------------------------------------
consteval std::vector<std::meta::info> method_thunk_specs(
    std::meta::info interface_type, std::meta::info owner_base_type) {
  std::vector<std::meta::info> specs;
  std::vector<std::string_view> seen_names;

  for (std::meta::info member :
       members_of(interface_type,
                  std::meta::access_context::unprivileged())) {
    if (!is_function(member)) continue;
    if (is_special_member_function(member)) continue;
    if (!has_identifier(member)) continue;

    std::string_view name = identifier_of(member);

    // Skip overloads: only the first declaration with a given name is
    // included.  See the comment above for why.
    bool already_seen = false;
    for (std::string_view seen : seen_names) {
      if (seen == name) {
        already_seen = true;
        break;
      }
    }
    if (already_seen) continue;
    seen_names.push_back(name);

    // Build the function-pointer type R(*)(Args...) from the method's
    // return type and parameter types.
    std::vector<std::meta::info> fn_args{dealias(return_type_of(member))};
    for (std::meta::info parameter : parameters_of(member)) {
      fn_args.push_back(dealias(type_of(parameter)));
    }
    std::meta::info fn_ptr_type = substitute(^^fn_ptr_t, fn_args);

    // Choose the thunk template based on the method's constness and
    // noexcept specification.
    std::meta::info thunk_template;
    if (is_const(member) && is_noexcept(member)) {
      thunk_template = ^^method_thunk_const_noexcept;
    } else if (is_const(member)) {
      thunk_template = ^^method_thunk_const;
    } else if (is_noexcept(member)) {
      thunk_template = ^^method_thunk_mutable_noexcept;
    } else {
      thunk_template = ^^method_thunk_mutable;
    }

    std::meta::info thunk_type =
        substitute(thunk_template, {fn_ptr_type, owner_base_type});

    specs.push_back(data_member_spec(
        thunk_type,
        std::meta::data_member_options{.name = name, .no_unique_address = true}));
  }
  return specs;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Returns true at compile time if every public member function declared in
// Interface is present in Concrete with a matching signature (name, return
// type, parameter types, constness, ref-qualifier, and noexcept).
// ---------------------------------------------------------------------------
template <typename Interface, typename Concrete>
consteval bool conforms_to() {
  for (std::meta::info interface_member :
       members_of(^^Interface, std::meta::access_context::unprivileged())) {
    if (!is_function(interface_member)) continue;
    if (!has_identifier(interface_member)) continue;
    bool found = false;
    for (std::meta::info concrete_member :
         members_of(^^Concrete, std::meta::access_context::unprivileged())) {
      if (!is_function(concrete_member)) continue;
      if (!has_identifier(concrete_member)) continue;
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

// ---------------------------------------------------------------------------
// Synthesised member-stub bases.
//
// protocol_member_stubs<T> and protocol_view_member_stubs<T> are
// incomplete classes that are completed by a consteval block inside the
// protocol / protocol_view class template body.  Each block calls
// define_aggregate to inject one [[no_unique_address]] thunk data member
// per public non-special member function of T, giving protocol and
// protocol_view the member-function call syntax described at the top of
// this file.
// ---------------------------------------------------------------------------

// Base injected into protocol<T, Alloc>.
template <typename T>
struct protocol_member_stubs;

// Base injected into protocol_view<T>.
template <typename T>
struct protocol_view_member_stubs;

// ---------------------------------------------------------------------------
// protocol<T, Allocator>
// ---------------------------------------------------------------------------
template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol : public protocol_member_stubs<T> {
  // Synthesise the member stubs for this specialisation.  The consteval
  // block runs during translation when the class template is instantiated.
  // define_aggregate completes protocol_member_stubs<T> by injecting one
  // thunk data member per method of T.  The thunk's OwnerBase parameter is
  // protocol_member_stubs<T> itself, matching the vanishing-this-pointer
  // requirement that the thunk is the offset-zero member of its base.
  consteval {
    define_aggregate(^^protocol_member_stubs<T>,
                     detail::method_thunk_specs(^^T, ^^protocol_member_stubs<T>));
  }

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

  // Construct from any type U that conforms to the Interface T.
  template <typename U>
    requires conforms_to_v<T, std::remove_cvref_t<U>> &&
             is_neither_protocol_nor_protocol_view<U>
  explicit protocol(U&& value);
};

// ---------------------------------------------------------------------------
// protocol_view<T>
// ---------------------------------------------------------------------------
template <typename T>
class protocol_view : public protocol_view_member_stubs<T> {
  // Synthesise the member stubs for this specialisation.
  consteval {
    define_aggregate(
        ^^protocol_view_member_stubs<T>,
        detail::method_thunk_specs(^^T, ^^protocol_view_member_stubs<T>));
  }

 public:
  // Special member functions.
  protocol_view() = delete;
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) = default;
  ~protocol_view() = default;

  // Construct from any type U that conforms to the Interface T.
  template <typename U>
    requires conforms_to_v<T, std::remove_cvref_t<U>> &&
             is_neither_protocol_nor_protocol_view<U>
  explicit protocol_view(const U& object);
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_H_

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
// Both `protocol` and `protocol_view` dispatch member function calls at
// runtime through a generated vtable; `protocol`'s vtable extends
// `protocol_view`'s with destroy/copy/move entries used for allocator-aware
// ownership. Vtable entries are named by mangling the interface member
// function's signature (see "name_mangling.h"), so an entry can be found by
// the signature it implements rather than by declaration order.
//
// Neither implementation currently supports operators other than operator().

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

#include "conformance.hh"
#include "member_function_thunks.hh"
#include "name_mangling.h"
#include "operator_thunks.hh"
#include "protocol_traits.hh"
#include "protocol_wrappers.hh"
#include "vtable.hh"

// clang-p2996 deprecates data_member_options::no_unique_address in favour of
// a fork-specific attributes member that GCC does not have, so the warning is
// silenced for this file rather than moving off the standard field.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace xyz::reflection {

// Returns `true` if `Candidate` is a structural subtype of `Interface`;
// otherwise returns `false`.
// TODO(jbcoe) Move is_protocol_conformant into the detail header.
template <typename Interface, typename Candidate>
consteval bool is_protocol_conformant() {
  static_assert(std::is_same_v<Interface, std::remove_cvref_t<Interface>>,
                "Interface must not be cv/ref-qualified: strip qualifiers at "
                "the call site with std::remove_cvref_t.");

  if constexpr (!std::is_same_v<Candidate, std::remove_cvref_t<Candidate>>) {
    return false;
  }

  // Checking for protocol interface conformance is O(N*M) over member counts,
  // assumed to be negligible at compile time.
  // TODO(jbcoe): Use set/map once there is library support for `constexpr`.
  // Calls `protocol_interface_function_infos` rather than reading
  // `protocol_interface_functions_of` so that the rejection of a
  // ref-qualified interface member is thrown from this function, where a
  // caller can catch it, instead of escaping a variable initializer.
  if constexpr (std::is_class_v<Candidate>) {
    auto interface_member_functions =
        detail::protocol_interface_function_infos<^^Interface>();
    auto candidate_member_functions =
        detail::conformance_candidates_of<^^Candidate>;

    return std::ranges::all_of(
        interface_member_functions, [&](std::meta::info interface_member) {
          return std::ranges::any_of(
              candidate_member_functions,
              [&](std::meta::info candidate_member) {
                return detail::member_function_conforms_to(candidate_member,
                                                           interface_member);
              });
        });
  }
  return false;
}

template <typename Interface, typename Candidate>
inline constexpr bool is_protocol_conformant_v =
    is_protocol_conformant<Interface, Candidate>();

template <typename Interface, typename Allocator, typename Candidate>
inline constexpr bool
    is_protocol_conformant_v<protocol<Interface, Allocator>, Candidate> =
        is_protocol_conformant<Interface, Candidate>();

template <typename Interface, typename Candidate>
inline constexpr bool has_conformant_special_members_v =
    (!std::is_copy_constructible_v<Interface> ||
     std::is_copy_constructible_v<Candidate>) &&
    (!std::is_move_constructible_v<Interface> ||
     std::is_move_constructible_v<Candidate>);

struct bad_protocol_cast : std::exception {
  constexpr const char* what() const noexcept override {
    return "bad protocol_cast";
  }
};

// ---------------------------------------------------------------------------
// protocol<I, Allocator>
//
// Owning, allocator-aware, value-semantic. Member functions are forwarded
// through the owning vtable (see `vtable` below). Calling a member function
// on a valueless (moved-from) `protocol` is a precondition violation.
// ---------------------------------------------------------------------------
template <is_valid_interface I, typename Alloc = std::allocator<std::byte>>
class protocol
    : public detail::protocol_wrappers_t<I, protocol<I, Alloc>,
                                         detail::vtable_t<I>,
                                         detail::const_policy::propagate> {
  using traits = std::allocator_traits<Alloc>;

  // When using allocators in a type-erased context, we must rebind
  // the allocator for whatever type the user provides.
  template <typename T>
  using rebound = traits::template rebind_alloc<T>;

  template <typename T>
  using rebound_traits = std::allocator_traits<rebound<T>>;

  static constexpr bool pocca =
      traits::propagate_on_container_copy_assignment::value;

  static constexpr bool pocma =
      traits::propagate_on_container_move_assignment::value;

  static constexpr bool pocs = traits::propagate_on_container_swap::value;

  static constexpr bool always_equal = traits::is_always_equal::value;

  template <typename T, typename TNorm = std::decay_t<T>, typename... Args>
  static constexpr TNorm* create(const Alloc& alloc, Args&&... args) {
    rebound<TNorm> new_alloc{alloc};

    auto obj = rebound_traits<TNorm>::allocate(new_alloc, 1);
    try {
      rebound_traits<TNorm>::construct(new_alloc, obj,
                                       std::forward<Args>(args)...);
    } catch (...) {
      rebound_traits<TNorm>::deallocate(new_alloc, obj, 1);
      throw;
    }

    return obj;
  }

  using view_vtable = detail::vtable_t<I>;

  // Extends the generated per-member-function vtable with the entries needed
  // for ownership. Because it derives from `view_vtable`, the synthesised
  // member function thunks (which take a `const view_vtable*`) can call
  // through a `const vtable*` unchanged.
  struct vtable : view_vtable {
    void (*destroy)(const Alloc& alloc, void* data);
    void* (*copy)(const Alloc& alloc, const void* data);
    void* (*move)(const Alloc& alloc, void* data);
  };

  // Builds the vtable for the stored type `TNorm`: the member function
  // entries call through to `TNorm`'s conforming member functions and the
  // ownership entries use the (rebound) allocator.
  template <typename T, typename TNorm = std::decay_t<T>>
  static consteval vtable make_vtable_for() {
    vtable vtable{};
    static_cast<view_vtable&>(vtable) = detail::make_view_vtable<I, TNorm>();

    vtable.destroy = +[](const Alloc& alloc, void* data) -> void {
      rebound<TNorm> new_alloc{alloc};
      auto* typed = static_cast<TNorm*>(data);
      rebound_traits<TNorm>::destroy(new_alloc, typed);
      rebound_traits<TNorm>::deallocate(new_alloc, typed, 1);
    };

    // Copy construction and assignment should only reach this
    // if the interface is copy constructible.
    vtable.copy = +[](const Alloc& alloc, const void* data) -> void* {
      if constexpr (std::is_copy_constructible_v<I>) {
        return create<TNorm>(alloc, *static_cast<const TNorm*>(data));
      } else {
        std::unreachable();
      }
    };

    // Move construction and assignment should only reach this
    // if the interface is move constructible.
    vtable.move = +[](const Alloc& alloc, void* data) -> void* {
      if constexpr (std::is_move_constructible_v<I>) {
        return create<TNorm>(alloc, std::move(*static_cast<TNorm*>(data)));
      } else {
        std::unreachable();
      }
    };

    return vtable;
  }

  // Creates a vtable for the type T.
  template <typename T>
  static constexpr vtable vtable_for = make_vtable_for<T>();

  // A no-op vtable that is the stand-in for a nullptr vtable. Prevents
  // redundant null checks in the special member functions. The member
  // function entries are left null: calling a member function on a
  // valueless protocol is a precondition violation.
  static consteval vtable make_null_vtable() {
    vtable result{};
    result.xyz_protocol_typeid = &typeid(void);
    result.destroy = +[](const Alloc&, void*) -> void {};
    result.copy = +[](const Alloc&, const void*) -> void* { return nullptr; };
    result.move = +[](const Alloc&, void*) -> void* { return nullptr; };
    return result;
  }

  static constexpr vtable null_vtable = make_null_vtable();

  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member, bool IsConst,
            bool IsNoexcept>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            bool IsConst, bool IsNoexcept>
  friend struct detail::operator_thunk;

  // Grants `protocol_view` access so that a view of a protocol can share its
  // vtable.
  template <is_valid_view_interface>
  friend class protocol_view;

  template <typename T, typename Protocol>
    requires(std::same_as<std::decay_t<Protocol>, protocol> &&
             is_protocol_conformant_v<I, std::decay_t<T>>)
  friend constexpr T protocol_cast(Protocol&& operand) {
    if (*operand.vtable_->xyz_protocol_typeid != typeid(T)) {
      throw bad_protocol_cast{};
    }

    using pointer_type =
        std::conditional_t<std::is_const_v<std::remove_reference_t<Protocol>>,
                           const std::decay_t<T>*, std::decay_t<T>*>;
    return std::forward_like<Protocol>(
        *static_cast<pointer_type>(operand.object_));
  }

  template <typename T>
    requires(is_protocol_conformant_v<I, T>)
  friend constexpr T* protocol_cast(protocol* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(T)) {
      return nullptr;
    }

    return static_cast<T*>(operand->object_);
  }

  template <typename T>
    requires(is_protocol_conformant_v<I, T>)
  friend constexpr const T* protocol_cast(const protocol* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(T)) {
      return nullptr;
    }

    return static_cast<const T*>(operand->object_);
  }

  friend constexpr const std::type_info& target_type(
      const protocol& p) noexcept {
    return *p.vtable_->xyz_protocol_typeid;
  }

  [[no_unique_address]] Alloc alloc_;

  void* object_ = nullptr;
  const vtable* vtable_ = &null_vtable;

 public:
  using allocator_type = Alloc;

  protocol() = delete;

  // Construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             is_protocol_conformant_v<I, TNorm> &&
             has_conformant_special_members_v<I, TNorm> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(T&& obj)
      : protocol(std::allocator_arg, Alloc{}, std::forward<T>(obj)) {}

  // Allocator-aware construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             has_conformant_special_members_v<I, TNorm> &&
             is_protocol_conformant_v<I, TNorm>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a),
        object_(create<T>(alloc_, std::forward<T>(obj))),
        vtable_(&vtable_for<T>) {}

  // In-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>, Args&&... args)
      : alloc_(a),
        object_(create<T>(alloc_, std::forward<Args>(args)...)),
        vtable_(&vtable_for<T>) {}

  // In-place construction needs this overload because templates cannot
  // deduce initializer lists.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>, il,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place init-list construction from conforming T.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : alloc_(a),
        object_(create<T>(alloc_, il, std::forward<Args>(args)...)),
        vtable_(&vtable_for<T>) {}

  constexpr ~protocol() { vtable_->destroy(alloc_, object_); }

  // Copy construction.
  constexpr protocol(const protocol& other)
    requires std::is_copy_constructible_v<I>
      : protocol(std::allocator_arg,
                 traits::select_on_container_copy_construction(other.alloc_),
                 other) {}

  // Allocator-aware copy construction.
  constexpr protocol(std::allocator_arg_t, const Alloc& a,
                     const protocol& other)
    requires std::is_copy_constructible_v<I>
      : alloc_(a),
        object_(other.vtable_->copy(alloc_, other.object_)),
        vtable_(other.vtable_) {}

  // Move construction.
  constexpr protocol(protocol&& other) noexcept
    requires std::is_move_constructible_v<I>
      : protocol(std::allocator_arg, other.alloc_, std::move(other)) {}

  // Allocator-aware move construction.
  constexpr protocol(std::allocator_arg_t, const Alloc& a,
                     protocol&& other) noexcept(always_equal)
    requires std::is_move_constructible_v<I>
      : alloc_(a), vtable_(other.vtable_) {
    if (always_equal || alloc_ == other.alloc_) {
      // Fast path, we can just do a pointer swap.
      object_ = other.object_;
    } else {
      // Slow path, we have to heap allocate and move construct.
      object_ = other.vtable_->move(alloc_, other.object_);
      other.vtable_->destroy(other.alloc_, other.object_);
    }

    other.object_ = nullptr;
    other.vtable_ = &null_vtable;
  }

  // Copy assignment.
  constexpr protocol& operator=(const protocol& other)
    requires std::is_copy_constructible_v<I>
  {
    if (this == &other) {
      return *this;
    }

    if constexpr (pocca) {
      // Allocate before destruction for strong exception safety.
      void* new_object = other.vtable_->copy(other.alloc_, other.object_);

      vtable_->destroy(alloc_, object_);
      object_ = new_object;
      alloc_ = other.alloc_;
    } else {
      void* new_object = other.vtable_->copy(alloc_, other.object_);
      vtable_->destroy(alloc_, object_);
      object_ = new_object;
    }
    vtable_ = other.vtable_;

    return *this;
  }

  // Move assignment.
  constexpr protocol& operator=(protocol&& other) noexcept(always_equal ||
                                                           pocma)
    requires std::is_move_constructible_v<I>
  {
    if (this == &other) {
      return *this;
    }

    if (always_equal || pocma || alloc_ == other.alloc_) {
      // Fast path: just swap the pointers and (conditionally) the allocators.
      vtable_->destroy(alloc_, object_);
      object_ = other.object_;
      if constexpr (pocma) {
        alloc_ = other.alloc_;
      }
    } else {
      // Slow path: heap construct and move the object directly. Allocate first
      // for strong exception safety.
      void* new_object = other.vtable_->move(alloc_, other.object_);
      vtable_->destroy(alloc_, object_);
      other.vtable_->destroy(other.alloc_, other.object_);

      object_ = new_object;
    }

    other.object_ = nullptr;
    vtable_ = std::exchange(other.vtable_, &null_vtable);

    return *this;
  }

  constexpr void swap(protocol& other) noexcept(always_equal || pocs) {
    if constexpr (!always_equal && !pocs) {
      // The behavior is undefined if the allocators are not equal.
      assert(alloc_ == other.alloc_ &&
             "allocators must compare equal or propagate on swap");
    }

    using std::swap;
    if constexpr (pocs) {
      swap(alloc_, other.alloc_);
    }
    swap(object_, other.object_);
    swap(vtable_, other.vtable_);
  }

  // Can be discovered by ADL for more optimal swapping than std::swap.
  friend constexpr void swap(protocol& lhs,
                             protocol& rhs) noexcept(always_equal || pocs) {
    return lhs.swap(rhs);
  }

  constexpr const Alloc& get_allocator() const { return alloc_; }

  constexpr bool valueless_after_move() const
    requires std::is_move_constructible_v<I>
  {
    return object_ == nullptr;
  }
};

// ---------------------------------------------------------------------------
// protocol_view<T>
//
// A non-owning reference with shallow const: every member function of `T` is
// exposed as a const member function of the view, so `const protocol_view<T>`
// does not restrict the interface. Use `protocol_view<const T>` to view a
// const object.
// ---------------------------------------------------------------------------
template <is_valid_view_interface T>
class protocol_view
    : public detail::protocol_wrappers_t<T, protocol_view<T>,
                                         detail::vtable_t<T>,
                                         detail::const_policy::all_const> {
 public:
  // The default constructor is deleted as a default constructed
  // `protocol_view` would be empty.
  protocol_view() = delete;

  // Remaining special member functions are defaulted.
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) noexcept = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) noexcept = default;
  ~protocol_view() = default;

  // Views a non-const object that conforms to the Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  protocol_view(U& object)
      : object_(static_cast<void*>(std::addressof(object))),
        vtable_(&detail::view_vtable_for<T, U>) {}

  // A view of a temporary would dangle.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  protocol_view(const U&&) = delete;

  // Views the object a `protocol<T>` owns, sharing its vtable.
  //
  // Precondition: `p` is not valueless.
  template <typename Alloc>
  protocol_view(protocol<T, Alloc>& p) noexcept
      : object_(p.object_), vtable_(p.vtable_) {}

  // A view of a temporary would dangle.
  template <typename Alloc>
  protocol_view(protocol<T, Alloc>&&) = delete;

 private:
  // Grants `protocol_view<const T>` access so it can share this view's
  // `object_`/`vtable_`.
  template <is_valid_view_interface>
  friend class protocol_view;

  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member, bool IsConst,
            bool IsNoexcept>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            bool IsConst, bool IsNoexcept>
  friend struct detail::operator_thunk;

  template <typename U>
    requires(is_protocol_conformant_v<T, std::decay_t<U>>)
  friend constexpr U& protocol_cast(protocol_view operand) {
    if (*operand.vtable_->xyz_protocol_typeid != typeid(U)) {
      throw bad_protocol_cast{};
    }

    return *static_cast<std::decay_t<U>*>(operand.object_);
  }

  template <typename U>
    requires(is_protocol_conformant_v<T, U>)
  friend constexpr U* protocol_cast(protocol_view* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(U)) {
      return nullptr;
    }

    return static_cast<U*>(operand->object_);
  }

  friend constexpr const std::type_info& target_type(protocol_view p) noexcept {
    return *p.vtable_->xyz_protocol_typeid;
  }

  // Non-owning pointer to the viewed object.
  void* object_ = nullptr;

  const detail::vtable_t<T>* vtable_;
};

// ---------------------------------------------------------------------------
// protocol_view<const T>
//
// Views a const object `T` and exposes the const member functions.
// ---------------------------------------------------------------------------
template <is_valid_interface T>
class protocol_view<const T>
    : public detail::protocol_wrappers_t<T, protocol_view<const T>,
                                         detail::vtable_t<T>,
                                         detail::const_policy::const_only> {
 public:
  // The default constructor is deleted as a default constructed
  // `protocol_view` would be empty.
  protocol_view() = delete;

  // Remaining special member functions are defaulted.
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) noexcept = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) noexcept = default;
  ~protocol_view() = default;

  // Views a (possibly const) object that conforms to the Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>)
  protocol_view(const U& object)
      : object_(static_cast<const void*>(std::addressof(object))),
        vtable_(&detail::view_vtable_for<T, U>) {}

  // A view of a temporary would dangle.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>)
  protocol_view(const U&&) = delete;

  // Views the object a `protocol<T>` owns, sharing its vtable.
  //
  // Precondition: `p` is not valueless.
  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>& p) noexcept
      : object_(p.object_), vtable_(p.vtable_) {}

  // A view of a temporary would dangle.
  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>&&) = delete;

  // Views the object a `protocol_view<T>` views, sharing its vtable. Taken
  // The argument `view` is passed by value as `protocol_view` is a non-owning
  // handle.
  constexpr protocol_view(protocol_view<T> view) noexcept
      : object_(view.object_), vtable_(view.vtable_) {}

 private:
  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member, bool IsConst,
            bool IsNoexcept>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            bool IsConst, bool IsNoexcept>
  friend struct detail::operator_thunk;

  template <typename U>
    requires(is_protocol_conformant_v<T, std::decay_t<U>>)
  friend constexpr const U& protocol_cast(protocol_view operand) {
    if (*operand.vtable_->xyz_protocol_typeid != typeid(U)) {
      throw bad_protocol_cast{};
    }

    return *static_cast<const std::decay_t<U>*>(operand.object_);
  }

  template <typename U>
    requires(is_protocol_conformant_v<T, U>)
  friend constexpr const U* protocol_cast(protocol_view* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(U)) {
      return nullptr;
    }

    return static_cast<const U*>(operand->object_);
  }

  friend constexpr const std::type_info& target_type(protocol_view p) noexcept {
    return *p.vtable_->xyz_protocol_typeid;
  }

  // Non-owning pointer to the viewed object.
  const void* object_ = nullptr;

  const detail::vtable_t<T>* vtable_;
};

}  // namespace xyz::reflection

#pragma GCC diagnostic pop
#endif  // XYZ_REFLECTION_PROTOCOL_HH_

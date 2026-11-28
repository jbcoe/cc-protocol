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
#ifndef XYZ_PROTOCOL_PROTOCOL_HH_
#define XYZ_PROTOCOL_PROTOCOL_HH_

// A C++26-reflection-based implementation of protocol and protocol_view.
//
// Both `protocol` and `protocol_view` dispatch member function calls at
// runtime through a generated vtable. `protocol`'s entries take its storage
// rather than the object's address, and its vtable adds destroy/copy/move
// entries used for allocator-aware ownership. Vtable entries are named by
// mangling the interface member function's signature (see
// "xyz/detail/name_mangling.hh"), so an entry can be found by the signature it
// implements rather than by declaration order.
//
// `protocol` stores a small object that its allocator can move without
// throwing in a buffer inside itself, and any other object on the heap (see
// "xyz/detail/small_buffer.hh"). Which one is an implementation detail. Either
// way the object is constructed and destroyed through the allocator, which
// allocates it only on the heap. A moved-from `protocol` is valueless, and
// moving, swapping or assigning to a `protocol` may invalidate views of and
// references to its object.
//
// Neither implementation currently supports operators other than operator().

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <meta>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "xyz/detail/conformance.hh"
#include "xyz/detail/conversion_thunks.hh"
#include "xyz/detail/member_function_thunks.hh"
#include "xyz/detail/name_mangling.hh"
#include "xyz/detail/operator_thunks.hh"
#include "xyz/detail/owning_vtable.hh"
#include "xyz/detail/protocol_traits.hh"
#include "xyz/detail/protocol_wrappers.hh"
#include "xyz/detail/small_buffer.hh"
#include "xyz/detail/vtable.hh"

namespace xyz {

template <detail::is_valid_interface T, typename Allocator>
class protocol;

template <detail::is_valid_view_interface T>
class protocol_view;

template <detail::is_valid_interface T, typename Allocator>
struct detail::is_protocol<xyz::protocol<T, Allocator>> : std::true_type {};

template <detail::is_valid_view_interface T>
struct detail::is_protocol_view<xyz::protocol_view<T>> : std::true_type {};

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

    return detail::all_members_conform(interface_member_functions,
                                       candidate_member_functions);
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
// through the owning vtable (see "xyz/detail/owning_vtable.hh"). Calling a
// member function on a valueless (moved-from) `protocol` is a precondition
// violation.
// ---------------------------------------------------------------------------
template <detail::is_valid_interface I,
          typename Alloc = std::allocator<std::byte>>
class protocol
    : public detail::protocol_wrappers_t<I, protocol<I, Alloc>,
                                         detail::storage_vtable_t<I>,
                                         detail::const_policy::propagate> {
  // The address of the stored object, or null if valueless. During constant
  // evaluation every object is on the heap (see "xyz/detail/small_buffer.hh").
  constexpr void* xyz_protocol_object_address() noexcept {
    if !consteval {
      if (xyz_protocol_vtable_->storage_kind == detail::storage_kind::buffer) {
        return xyz_protocol_storage_.buffer_.data();
      }
    }
    return xyz_protocol_storage_.pointer_;
  }

  constexpr const void* xyz_protocol_object_address() const noexcept {
    if !consteval {
      if (xyz_protocol_vtable_->storage_kind == detail::storage_kind::buffer) {
        return xyz_protocol_storage_.buffer_.data();
      }
    }
    return xyz_protocol_storage_.pointer_;
  }

  // Grants the synthesised member thunks access to `xyz_protocol_vtable_` and
  // `xyz_protocol_storage_` so they can locate and call through the matching
  // vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::xyz_protocol_operator_thunk;

  template <typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::xyz_protocol_conversion_thunk;

  // Grants `protocol_view` access so that a view of a protocol can find the
  // object and its vtable.
  template <detail::is_valid_view_interface>
  friend class protocol_view;

  template <typename T, typename Protocol>
    requires(std::same_as<std::decay_t<Protocol>, protocol> &&
             is_protocol_conformant_v<I, std::decay_t<T>>)
  friend constexpr T protocol_cast(Protocol&& operand) {
    if (*operand.xyz_protocol_vtable_->xyz_protocol_typeid != typeid(T)) {
      throw bad_protocol_cast{};
    }

    using pointer_type =
        std::conditional_t<std::is_const_v<std::remove_reference_t<Protocol>>,
                           const std::decay_t<T>*, std::decay_t<T>*>;
    return std::forward_like<Protocol>(*std::launder(
        static_cast<pointer_type>(operand.xyz_protocol_object_address())));
  }

  template <typename T>
    requires(is_protocol_conformant_v<I, T>)
  friend constexpr T* protocol_cast(protocol* operand) noexcept {
    if (*operand->xyz_protocol_vtable_->xyz_protocol_typeid != typeid(T)) {
      return nullptr;
    }

    return std::launder(
        static_cast<T*>(operand->xyz_protocol_object_address()));
  }

  template <typename T>
    requires(is_protocol_conformant_v<I, T>)
  friend constexpr const T* protocol_cast(const protocol* operand) noexcept {
    if (*operand->xyz_protocol_vtable_->xyz_protocol_typeid != typeid(T)) {
      return nullptr;
    }

    return std::launder(
        static_cast<const T*>(operand->xyz_protocol_object_address()));
  }

  friend constexpr const std::type_info& target_type(
      const protocol& p) noexcept {
    return *p.xyz_protocol_vtable_->xyz_protocol_typeid;
  }

  // The pointer the synthesised member thunks pass to the vtable entries.
  // Private, so not part of the interface; prefixed, as a private member name
  // still hides an interface member function of the same name.
  constexpr detail::small_buffer_storage*
  xyz_protocol_erased_object() noexcept {
    return &xyz_protocol_storage_;
  }

  constexpr const detail::small_buffer_storage* xyz_protocol_erased_object()
      const noexcept {
    return &xyz_protocol_storage_;
  }

  // Delegated to by the allocator-aware in-place constructors.
  template <typename T, typename... Args>
  constexpr explicit protocol(detail::construct_in_storage_t, const Alloc& a,
                              std::in_place_type_t<T>, Args&&... args)
      : xyz_protocol_allocator_(a) {
    detail::small_buffer_operations<T, Alloc>::construct(
        xyz_protocol_allocator_, xyz_protocol_storage_,
        std::forward<Args>(args)...);
    xyz_protocol_vtable_ = &detail::owning_vtable_for<I, Alloc, T>;
  }

  [[no_unique_address]] Alloc xyz_protocol_allocator_;

  const detail::owning_vtable<I, Alloc>* xyz_protocol_vtable_ =
      &detail::null_owning_vtable<I, Alloc>;

  // The owned object, or a pointer to it (see "xyz/detail/small_buffer.hh").
  detail::small_buffer_storage xyz_protocol_storage_;

 public:
  using allocator_type = Alloc;

  protocol() = delete;

  // Construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             is_protocol_conformant_v<I, TNorm> &&
             std::constructible_from<TNorm, T> &&
             has_conformant_special_members_v<I, TNorm> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(T&& obj)
      : protocol(std::allocator_arg, Alloc{}, std::forward<T>(obj)) {}

  // Allocator-aware construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             has_conformant_special_members_v<I, TNorm> &&
             is_protocol_conformant_v<I, TNorm> &&
             std::constructible_from<TNorm, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a, T&& obj)
      : protocol(detail::construct_in_storage, a, std::in_place_type<TNorm>,
                 std::forward<T>(obj)) {}

  // In-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::constructible_from<T, Args...> &&
             has_conformant_special_members_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::constructible_from<T, Args...> &&
             has_conformant_special_members_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>, Args&&... args)
      : protocol(detail::construct_in_storage, a, std::in_place_type<T>,
                 std::forward<Args>(args)...) {}

  // In-place construction needs this overload because templates cannot
  // deduce initializer lists.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::constructible_from<T, std::initializer_list<U>&, Args...> &&
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
             std::constructible_from<T, std::initializer_list<U>&, Args...> &&
             has_conformant_special_members_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : protocol(detail::construct_in_storage, a, std::in_place_type<T>, il,
                 std::forward<Args>(args)...) {}

  constexpr ~protocol() {
    xyz_protocol_vtable_->destroy(xyz_protocol_allocator_,
                                  xyz_protocol_storage_);
  }

  // Copy construction.
  constexpr protocol(const protocol& other)
    requires std::is_copy_constructible_v<I>
      : protocol(
            std::allocator_arg,
            std::allocator_traits<Alloc>::select_on_container_copy_construction(
                other.xyz_protocol_allocator_),
            other) {}

  // Allocator-aware copy construction.
  constexpr protocol(std::allocator_arg_t, const Alloc& a,
                     const protocol& other)
    requires std::is_copy_constructible_v<I>
      : xyz_protocol_allocator_(a) {
    other.xyz_protocol_vtable_->copy(xyz_protocol_allocator_,
                                     other.xyz_protocol_storage_,
                                     xyz_protocol_storage_);
    xyz_protocol_vtable_ = other.xyz_protocol_vtable_;
  }

  // Move construction.
  constexpr protocol(protocol&& other) noexcept
    requires std::is_move_constructible_v<I>
      : protocol(std::allocator_arg, other.xyz_protocol_allocator_,
                 std::move(other)) {}

  // Allocator-aware move construction.
  constexpr protocol(
      std::allocator_arg_t, const Alloc& a,
      protocol&& other) noexcept(detail::allocator_is_always_equal_v<Alloc>)
    requires std::is_move_constructible_v<I>
      : xyz_protocol_allocator_(a) {
    if (detail::allocator_is_always_equal_v<Alloc> ||
        xyz_protocol_allocator_ == other.xyz_protocol_allocator_) {
      // Fast path: take the heap-allocated object, or move the object out of
      // the small buffer.
      other.xyz_protocol_vtable_->relocate(other.xyz_protocol_allocator_,
                                           other.xyz_protocol_storage_,
                                           xyz_protocol_storage_);
    } else {
      // Slow path: move construct, allocating with our allocator if the
      // object is not stored in the small buffer.
      other.xyz_protocol_vtable_->move(xyz_protocol_allocator_,
                                       other.xyz_protocol_storage_,
                                       xyz_protocol_storage_);
      other.xyz_protocol_vtable_->destroy(other.xyz_protocol_allocator_,
                                          other.xyz_protocol_storage_);
    }

    xyz_protocol_vtable_ = std::exchange(other.xyz_protocol_vtable_,
                                         &detail::null_owning_vtable<I, Alloc>);
  }

  // Copy assignment.
  constexpr protocol& operator=(const protocol& other)
    requires std::is_copy_constructible_v<I>
  {
    if (this == std::addressof(other)) {
      return *this;
    }

    // Copy before destruction for strong exception safety. `other` may be
    // owned by the object this protocol holds, so nothing is read from it
    // after that object is destroyed.
    const detail::owning_vtable<I, Alloc>* new_vtable =
        other.xyz_protocol_vtable_;
    detail::small_buffer_storage copy;
    if constexpr (detail::propagate_on_copy_assignment_v<Alloc>) {
      new_vtable->copy(other.xyz_protocol_allocator_,
                       other.xyz_protocol_storage_, copy);
      Alloc new_alloc = other.xyz_protocol_allocator_;
      xyz_protocol_vtable_->destroy(xyz_protocol_allocator_,
                                    xyz_protocol_storage_);
      xyz_protocol_allocator_ = new_alloc;
    } else {
      new_vtable->copy(xyz_protocol_allocator_, other.xyz_protocol_storage_,
                       copy);
      xyz_protocol_vtable_->destroy(xyz_protocol_allocator_,
                                    xyz_protocol_storage_);
    }
    new_vtable->relocate(xyz_protocol_allocator_, copy, xyz_protocol_storage_);
    xyz_protocol_vtable_ = new_vtable;

    return *this;
  }

  // Move assignment.
  constexpr protocol& operator=(protocol&& other) noexcept(
      detail::allocator_is_always_equal_v<Alloc> ||
      detail::propagate_on_move_assignment_v<Alloc>)
    requires std::is_move_constructible_v<I>
  {
    if (this == std::addressof(other)) {
      return *this;
    }

    // `other` may be owned by the object this protocol holds, so it is left
    // valueless before that object is destroyed and is not touched after.
    const detail::owning_vtable<I, Alloc>* new_vtable =
        other.xyz_protocol_vtable_;
    detail::small_buffer_storage moved;
    if (detail::allocator_is_always_equal_v<Alloc> ||
        detail::propagate_on_move_assignment_v<Alloc> ||
        xyz_protocol_allocator_ == other.xyz_protocol_allocator_) {
      // Fast path: take the object.
      new_vtable->relocate(other.xyz_protocol_allocator_,
                           other.xyz_protocol_storage_, moved);
    } else {
      // Slow path: move construct with our allocator. Allocate first for
      // strong exception safety.
      new_vtable->move(xyz_protocol_allocator_, other.xyz_protocol_storage_,
                       moved);
      new_vtable->destroy(other.xyz_protocol_allocator_,
                          other.xyz_protocol_storage_);
    }
    other.xyz_protocol_vtable_ = &detail::null_owning_vtable<I, Alloc>;

    if constexpr (detail::propagate_on_move_assignment_v<Alloc>) {
      Alloc new_alloc = other.xyz_protocol_allocator_;
      xyz_protocol_vtable_->destroy(xyz_protocol_allocator_,
                                    xyz_protocol_storage_);
      xyz_protocol_allocator_ = new_alloc;
    } else {
      xyz_protocol_vtable_->destroy(xyz_protocol_allocator_,
                                    xyz_protocol_storage_);
    }
    new_vtable->relocate(xyz_protocol_allocator_, moved, xyz_protocol_storage_);
    xyz_protocol_vtable_ = new_vtable;

    return *this;
  }

  // Assignment from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> && !detail::is_protocol_v<TNorm> &&
             is_protocol_conformant_v<I, TNorm> &&
             std::constructible_from<TNorm, T> &&
             has_conformant_special_members_v<I, TNorm>)
  constexpr protocol& operator=(T&& obj) {
    // Construct before destruction for strong exception safety. `obj` may be
    // owned by the object this protocol holds, so it is not read after that
    // object is destroyed.
    detail::small_buffer_storage constructed;
    detail::small_buffer_operations<TNorm, Alloc>::construct(
        xyz_protocol_allocator_, constructed, std::forward<T>(obj));
    xyz_protocol_vtable_->destroy(xyz_protocol_allocator_,
                                  xyz_protocol_storage_);
    const auto* new_vtable = &detail::owning_vtable_for<I, Alloc, TNorm>;
    new_vtable->relocate(xyz_protocol_allocator_, constructed,
                         xyz_protocol_storage_);
    xyz_protocol_vtable_ = new_vtable;

    return *this;
  }

  constexpr void swap(protocol& other) noexcept(
      detail::allocator_is_always_equal_v<Alloc> ||
      detail::propagate_on_swap_v<Alloc>) {
    // Relocating out of an emptied storage would read an object that is no
    // longer there.
    if (this == std::addressof(other)) {
      return;
    }

    if constexpr (!detail::allocator_is_always_equal_v<Alloc> &&
                  !detail::propagate_on_swap_v<Alloc>) {
      // The behavior is undefined if the allocators are not equal.
      assert(xyz_protocol_allocator_ == other.xyz_protocol_allocator_ &&
             "allocators must compare equal or propagate on swap");
    }

    // The storages are swapped through their objects' relocate entries rather
    // than as values, so that an object stored in place is moved properly.
    // Each object is relocated with the allocator it was stored with.
    detail::small_buffer_storage storage;
    xyz_protocol_vtable_->relocate(xyz_protocol_allocator_,
                                   xyz_protocol_storage_, storage);
    other.xyz_protocol_vtable_->relocate(other.xyz_protocol_allocator_,
                                         other.xyz_protocol_storage_,
                                         xyz_protocol_storage_);
    xyz_protocol_vtable_->relocate(xyz_protocol_allocator_, storage,
                                   other.xyz_protocol_storage_);

    using std::swap;
    if constexpr (detail::propagate_on_swap_v<Alloc>) {
      swap(xyz_protocol_allocator_, other.xyz_protocol_allocator_);
    }
    swap(xyz_protocol_vtable_, other.xyz_protocol_vtable_);
  }

  // Can be discovered by ADL for more optimal swapping than std::swap.
  friend constexpr void swap(protocol& lhs, protocol& rhs) noexcept(
      detail::allocator_is_always_equal_v<Alloc> ||
      detail::propagate_on_swap_v<Alloc>) {
    return lhs.swap(rhs);
  }

  constexpr const Alloc& get_allocator() const {
    return xyz_protocol_allocator_;
  }

  // Hidden friend rather than a member so it can't collide with a forwarded
  // interface member function of the same name.
  friend constexpr bool valueless_after_move(const protocol& p) noexcept {
    return p.xyz_protocol_vtable_->storage_kind == detail::storage_kind::none;
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
template <detail::is_valid_view_interface T>
class protocol_view
    : public detail::protocol_wrappers_t<T, protocol_view<T>,
                                         detail::vtable_t<T>,
                                         detail::const_policy::constify> {
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
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  protocol_view(U& object)
      : xyz_protocol_object_(static_cast<void*>(std::addressof(object))),
        xyz_protocol_vtable_(&detail::view_vtable_for<T, U>) {}

  // A view of a temporary would dangle.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  protocol_view(const U&&) = delete;

  // Views the object a `protocol<T>` owns.
  //
  // Precondition: `p` is not valueless.
  // Moving, swapping or assigning to `p` may invalidate the view.
  template <typename Alloc>
  protocol_view(protocol<T, Alloc>& p) noexcept
      : xyz_protocol_object_(p.xyz_protocol_object_address()),
        xyz_protocol_vtable_(p.xyz_protocol_vtable_->object_vtable) {}

  // A view of a temporary would dangle.
  template <typename Alloc>
  protocol_view(protocol<T, Alloc>&&) = delete;

 private:
  // Grants `protocol_view<const T>` access so it can share this view's
  // `xyz_protocol_object_`/`xyz_protocol_vtable_`.
  template <detail::is_valid_view_interface>
  friend class protocol_view;

  // Grants the synthesised member thunks access to
  // `xyz_protocol_object_`/`xyz_protocol_vtable_` so they can locate and call
  // through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::xyz_protocol_operator_thunk;

  template <typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::xyz_protocol_conversion_thunk;

  template <typename U>
    requires(is_protocol_conformant_v<T, std::decay_t<U>>)
  friend constexpr U& protocol_cast(protocol_view operand) {
    if (*operand.xyz_protocol_vtable_->xyz_protocol_typeid != typeid(U)) {
      throw bad_protocol_cast{};
    }

    return *std::launder(
        static_cast<std::decay_t<U>*>(operand.xyz_protocol_object_));
  }

  template <typename U>
    requires(is_protocol_conformant_v<T, U>)
  friend constexpr U* protocol_cast(protocol_view* operand) noexcept {
    if (*operand->xyz_protocol_vtable_->xyz_protocol_typeid != typeid(U)) {
      return nullptr;
    }

    return std::launder(static_cast<U*>(operand->xyz_protocol_object_));
  }

  friend constexpr const std::type_info& target_type(protocol_view p) noexcept {
    return *p.xyz_protocol_vtable_->xyz_protocol_typeid;
  }

  // The pointer the synthesised member thunks pass to the vtable entries. The
  // view's const is shallow, so a const view passes a mutable pointer.
  // Private and prefixed, as for `protocol`.
  constexpr void* xyz_protocol_erased_object() const noexcept {
    return xyz_protocol_object_;
  }

  // Non-owning pointer to the viewed object.
  void* xyz_protocol_object_ = nullptr;

  const detail::vtable_t<T>* xyz_protocol_vtable_;
};

// ---------------------------------------------------------------------------
// protocol_view<const T>
//
// Views a const object `T` and exposes the const member functions.
// ---------------------------------------------------------------------------
template <detail::is_valid_interface T>
class protocol_view<const T>
    : public detail::protocol_wrappers_t<T, protocol_view<const T>,
                                         detail::vtable_t<T>,
                                         detail::const_policy::only_const> {
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
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>)
  protocol_view(const U& object)
      : xyz_protocol_object_(static_cast<const void*>(std::addressof(object))),
        xyz_protocol_vtable_(&detail::view_vtable_for<T, U>) {}

  // A view of a temporary would dangle.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>)
  protocol_view(const U&&) = delete;

  // Views the object a `protocol<T>` owns.
  //
  // Precondition: `p` is not valueless.
  // Moving, swapping or assigning to `p` may invalidate the view.
  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>& p) noexcept
      : xyz_protocol_object_(p.xyz_protocol_object_address()),
        xyz_protocol_vtable_(p.xyz_protocol_vtable_->object_vtable) {}

  // A view of a temporary would dangle.
  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>&&) = delete;

  // Views the object a `protocol_view<T>` views, sharing its vtable.
  // The argument `view` is passed by value as `protocol_view` is a non-owning
  // handle. A template so that overload resolution for another argument does
  // not complete `protocol_view<T>`, which rejects some interfaces that
  // `protocol_view<const T>` supports.
  template <std::same_as<protocol_view<T>> View>
  constexpr protocol_view(View view) noexcept
      : xyz_protocol_object_(view.xyz_protocol_object_),
        xyz_protocol_vtable_(view.xyz_protocol_vtable_) {}

 private:
  // Grants the synthesised member thunks access to
  // `xyz_protocol_object_`/`xyz_protocol_vtable_` so they can locate and call
  // through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::xyz_protocol_operator_thunk;

  template <typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::xyz_protocol_conversion_thunk;

  template <typename U>
    requires(is_protocol_conformant_v<T, std::decay_t<U>>)
  friend constexpr const U& protocol_cast(protocol_view operand) {
    if (*operand.xyz_protocol_vtable_->xyz_protocol_typeid != typeid(U)) {
      throw bad_protocol_cast{};
    }

    return *std::launder(
        static_cast<const std::decay_t<U>*>(operand.xyz_protocol_object_));
  }

  template <typename U>
    requires(is_protocol_conformant_v<T, U>)
  friend constexpr const U* protocol_cast(protocol_view* operand) noexcept {
    if (*operand->xyz_protocol_vtable_->xyz_protocol_typeid != typeid(U)) {
      return nullptr;
    }

    return std::launder(static_cast<const U*>(operand->xyz_protocol_object_));
  }

  friend constexpr const std::type_info& target_type(protocol_view p) noexcept {
    return *p.xyz_protocol_vtable_->xyz_protocol_typeid;
  }

  // The pointer the synthesised member thunks pass to the vtable entries.
  // Private and prefixed, as for `protocol`.
  constexpr const void* xyz_protocol_erased_object() const noexcept {
    return xyz_protocol_object_;
  }

  // Non-owning pointer to the viewed object.
  const void* xyz_protocol_object_ = nullptr;

  const detail::vtable_t<T>* xyz_protocol_vtable_;
};

}  // namespace xyz

#endif  // XYZ_PROTOCOL_PROTOCOL_HH_

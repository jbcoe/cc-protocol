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
// runtime through a generated vtable; `protocol`'s vtable extends
// `protocol_view`'s with destroy/copy/move entries used for allocator-aware
// ownership. Vtable entries are named by mangling the interface member
// function's signature (see "xyz/detail/name_mangling.hh"), so an entry can
// be found by the signature it implements rather than by declaration order.
//
// `protocol` stores a small, nothrow-movable object in a buffer inside itself
// and any other object on the heap (see "xyz/detail/small_buffer.hh"). Which
// one is an implementation detail. Either way a moved-from `protocol` is
// valueless, and a view of, or reference to, an object in the buffer is
// invalidated when the `protocol` is moved, swapped or assigned to.
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
// through the owning vtable (see `vtable` below). Calling a member function
// on a valueless (moved-from) `protocol` is a precondition violation.
// ---------------------------------------------------------------------------
template <detail::is_valid_interface I,
          typename Alloc = std::allocator<std::byte>>
class protocol
    : public detail::protocol_wrappers_t<I, protocol<I, Alloc>,
                                         detail::vtable_t<I>,
                                         detail::const_policy::propagate> {
  using traits = std::allocator_traits<Alloc>;

  static constexpr bool pocca =
      traits::propagate_on_container_copy_assignment::value;

  static constexpr bool pocma =
      traits::propagate_on_container_move_assignment::value;

  static constexpr bool pocs = traits::propagate_on_container_swap::value;

  static constexpr bool always_equal = traits::is_always_equal::value;

  using view_vtable = detail::vtable_t<I>;

  // Extends the generated per-member-function vtable with the entries needed
  // for ownership. Because it derives from `view_vtable`, the synthesised
  // member function thunks (which take a `const view_vtable*`) can call
  // through a `const vtable*` unchanged.
  //
  // The member function entries take the address of `storage_`. Whether the
  // object is in the small buffer or on the heap is fixed for each stored
  // type, so each type's entries know where to find it without a branch.
  struct vtable : view_vtable {
    // Destroys the object and leaves the storage empty.
    void (*destroy)(const Alloc& alloc,
                    detail::small_buffer_storage& storage) noexcept;
    // Constructs a copy of the object in empty `destination`.
    void (*copy)(const Alloc& alloc, const detail::small_buffer_storage& source,
                 detail::small_buffer_storage& destination);
    // Move-constructs the object into empty `destination`, using `alloc` for
    // a heap-allocated object.
    void (*move)(const Alloc& alloc, detail::small_buffer_storage& source,
                 detail::small_buffer_storage& destination);
    // Moves the object to empty `destination`, which uses an allocator equal
    // to the source's, and leaves the source empty.
    void (*relocate)(detail::small_buffer_storage& source,
                     detail::small_buffer_storage& destination) noexcept;
    // The vtable a `protocol_view` of the stored object uses: its entries
    // take the object's address, not the address of `storage_`.
    const view_vtable* object_vtable;
    // Whether the object is in the small buffer outside constant evaluation.
    bool is_buffered;
  };

  // Builds the vtable for the stored type `TNorm`: the member function
  // entries call through to `TNorm`'s conforming member functions and the
  // ownership entries store it in the small buffer or on the heap (see
  // "xyz/detail/small_buffer.hh").
  template <typename T, typename TNorm = std::decay_t<T>>
  static consteval vtable make_vtable_for() {
    using operations = detail::small_buffer_operations<TNorm, Alloc>;

    vtable vtable{};
    static_cast<view_vtable&>(vtable) =
        detail::make_view_vtable<I, TNorm, !operations::fits>();

    vtable.destroy = &operations::destroy;

    // Copy construction and assignment should only reach this
    // if the interface is copy constructible.
    vtable.copy =
        +[](const Alloc& alloc, const detail::small_buffer_storage& source,
            detail::small_buffer_storage& destination) {
          if constexpr (std::is_copy_constructible_v<I>) {
            operations::copy_construct(alloc, source, destination);
          } else {
            std::unreachable();
          }
        };

    // Move construction and assignment should only reach this
    // if the interface is move constructible.
    vtable.move = +[](const Alloc& alloc, detail::small_buffer_storage& source,
                      detail::small_buffer_storage& destination) {
      if constexpr (std::is_move_constructible_v<I>) {
        operations::move_construct(alloc, source, destination);
      } else {
        std::unreachable();
      }
    };

    // Not limited to move constructible interfaces: copy assignment
    // relocates a copy into place.
    vtable.relocate = &operations::relocate;

    vtable.object_vtable = &detail::view_vtable_for<I, TNorm>;
    vtable.is_buffered = operations::fits;

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
    static_cast<view_vtable&>(result) = detail::make_null_view_vtable<I>();
    result.destroy =
        +[](const Alloc&, detail::small_buffer_storage&) noexcept {};
    result.copy = +[](const Alloc&, const detail::small_buffer_storage&,
                      detail::small_buffer_storage& destination) {
      destination.pointer_ = nullptr;
    };
    result.move = +[](const Alloc&, detail::small_buffer_storage&,
                      detail::small_buffer_storage& destination) {
      destination.pointer_ = nullptr;
    };
    result.relocate = +[](detail::small_buffer_storage&,
                          detail::small_buffer_storage& destination) noexcept {
      destination.pointer_ = nullptr;
    };
    result.object_vtable = &detail::null_view_vtable_for<I>;
    result.is_buffered = false;
    return result;
  }

  static constexpr vtable null_vtable = make_null_vtable();

  // The address of the stored object, or null if valueless. Prefixed, as a
  // private member name still hides an interface member function of the same
  // name.
  constexpr void* xyz_protocol_object_address() noexcept {
    if !consteval {
      if (vtable_->is_buffered) {
        return storage_.buffer_.data();
      }
    }
    return storage_.pointer_;
  }

  constexpr const void* xyz_protocol_object_address() const noexcept {
    if !consteval {
      if (vtable_->is_buffered) {
        return storage_.buffer_.data();
      }
    }
    return storage_.pointer_;
  }

  // Replaces the stored object with `source`'s and leaves `source` valueless.
  // Requires the allocators to compare equal unless `PropagateAllocator`, in
  // which case `source`'s allocator replaces this one.
  template <bool PropagateAllocator>
  constexpr void xyz_protocol_move_from(protocol& source) noexcept {
    vtable_->destroy(alloc_, storage_);
    if constexpr (PropagateAllocator) {
      alloc_ = source.alloc_;
    }
    source.vtable_->relocate(source.storage_, storage_);
    vtable_ = std::exchange(source.vtable_, &null_vtable);
  }

  // Grants the synthesised member thunks access to `vtable_` so they can
  // locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::operator_thunk;

  template <typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::conversion_thunk;

  // Grants `protocol_view` access so that a view of a protocol can find the
  // object and its vtable.
  template <detail::is_valid_view_interface>
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
    return std::forward_like<Protocol>(*std::launder(
        static_cast<pointer_type>(operand.xyz_protocol_object_address())));
  }

  template <typename T>
    requires(is_protocol_conformant_v<I, T>)
  friend constexpr T* protocol_cast(protocol* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(T)) {
      return nullptr;
    }

    return std::launder(
        static_cast<T*>(operand->xyz_protocol_object_address()));
  }

  template <typename T>
    requires(is_protocol_conformant_v<I, T>)
  friend constexpr const T* protocol_cast(const protocol* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(T)) {
      return nullptr;
    }

    return std::launder(
        static_cast<const T*>(operand->xyz_protocol_object_address()));
  }

  friend constexpr const std::type_info& target_type(
      const protocol& p) noexcept {
    return *p.vtable_->xyz_protocol_typeid;
  }

  // The pointer the synthesised member thunks pass to the vtable entries.
  // Private, so not part of the interface; prefixed, as a private member name
  // still hides an interface member function of the same name.
  constexpr void* xyz_protocol_erased_object() noexcept { return &storage_; }

  constexpr const void* xyz_protocol_erased_object() const noexcept {
    return &storage_;
  }

  // Delegated to by the allocator-aware in-place constructors.
  template <typename T, typename... Args>
  constexpr explicit protocol(
      detail::construct_in_storage_t, const Alloc& a, std::in_place_type_t<T>,
      Args&&... args) noexcept(detail::is_nothrow_storable_v<T, Alloc, Args...>)
      : alloc_(a) {
    detail::small_buffer_operations<T, Alloc>::construct(
        alloc_, storage_, std::forward<Args>(args)...);
    vtable_ = &vtable_for<T>;
  }

  [[no_unique_address]] Alloc alloc_;

  const vtable* vtable_ = &null_vtable;

  // The owned object, or a pointer to it (see "xyz/detail/small_buffer.hh").
  detail::small_buffer_storage storage_;

 public:
  using allocator_type = Alloc;

  protocol() = delete;

  // The constructors from a conforming T strengthen their exception
  // specification ([res.on.exception.handling]): they cannot throw if T is
  // stored in the small buffer and its constructor cannot throw. Whether T is
  // stored in the small buffer is up to the implementation, so this is not
  // part of the proposed specification and must not be relied on by portable
  // code.

  // Construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             is_protocol_conformant_v<I, TNorm> &&
             has_conformant_special_members_v<I, TNorm> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(T&& obj) noexcept(
      detail::is_nothrow_default_storable_v<TNorm, Alloc, T>)
      : protocol(std::allocator_arg, Alloc{}, std::forward<T>(obj)) {}

  // Allocator-aware construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             has_conformant_special_members_v<I, TNorm> &&
             is_protocol_conformant_v<I, TNorm>)
  constexpr explicit protocol(
      std::allocator_arg_t, const Alloc& a,
      T&& obj) noexcept(detail::is_nothrow_storable_v<TNorm, Alloc, T>)
      : protocol(detail::construct_in_storage, a, std::in_place_type<TNorm>,
                 std::forward<T>(obj)) {}

  // In-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>, Args&&... args) noexcept(
      detail::is_nothrow_default_storable_v<T, Alloc, Args...>)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T>)
  constexpr explicit protocol(
      std::allocator_arg_t, const Alloc& a, std::in_place_type_t<T>,
      Args&&... args) noexcept(detail::is_nothrow_storable_v<T, Alloc, Args...>)
      : protocol(detail::construct_in_storage, a, std::in_place_type<T>,
                 std::forward<Args>(args)...) {}

  // In-place construction needs this overload because templates cannot
  // deduce initializer lists.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(
      std::in_place_type_t<T>, std::initializer_list<U> il,
      Args&&... args) noexcept(detail::
                                   is_nothrow_default_storable_v<
                                       T, Alloc, std::initializer_list<U>&,
                                       Args...>)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>, il,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place init-list construction from conforming T.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             has_conformant_special_members_v<I, T>)
  constexpr explicit protocol(
      std::allocator_arg_t, const Alloc& a, std::in_place_type_t<T>,
      std::initializer_list<U> il,
      Args&&... args) noexcept(detail::
                                   is_nothrow_storable_v<
                                       T, Alloc, std::initializer_list<U>&,
                                       Args...>)
      : protocol(detail::construct_in_storage, a, std::in_place_type<T>, il,
                 std::forward<Args>(args)...) {}

  constexpr ~protocol() { vtable_->destroy(alloc_, storage_); }

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
      : alloc_(a) {
    other.vtable_->copy(alloc_, other.storage_, storage_);
    vtable_ = other.vtable_;
  }

  // Move construction.
  constexpr protocol(protocol&& other) noexcept
    requires std::is_move_constructible_v<I>
      : protocol(std::allocator_arg, other.alloc_, std::move(other)) {}

  // Allocator-aware move construction.
  constexpr protocol(std::allocator_arg_t, const Alloc& a,
                     protocol&& other) noexcept(always_equal)
    requires std::is_move_constructible_v<I>
      : alloc_(a) {
    if (always_equal || alloc_ == other.alloc_) {
      // Fast path: take the heap-allocated object, or move the object out of
      // the small buffer.
      other.vtable_->relocate(other.storage_, storage_);
    } else {
      // Slow path: move construct, allocating with our allocator if the
      // object is not stored in the small buffer.
      other.vtable_->move(alloc_, other.storage_, storage_);
      other.vtable_->destroy(other.alloc_, other.storage_);
    }

    vtable_ = std::exchange(other.vtable_, &null_vtable);
  }

  // Copy assignment.
  constexpr protocol& operator=(const protocol& other)
    requires std::is_copy_constructible_v<I>
  {
    if (this == std::addressof(other)) {
      return *this;
    }

    // Copy before destruction for strong exception safety: the copy cannot be
    // constructed in place, as the small buffer may hold the current object.
    protocol copy(std::allocator_arg, pocca ? other.alloc_ : alloc_, other);
    xyz_protocol_move_from<pocca>(copy);

    return *this;
  }

  // Move assignment.
  constexpr protocol& operator=(protocol&& other) noexcept(always_equal ||
                                                           pocma)
    requires std::is_move_constructible_v<I>
  {
    if (this == std::addressof(other)) {
      return *this;
    }

    if (always_equal || pocma || alloc_ == other.alloc_) {
      // Fast path: take the object and (conditionally) the allocator.
      xyz_protocol_move_from<pocma>(other);
    } else {
      // Slow path: move construct with our allocator before destruction for
      // strong exception safety.
      protocol moved(std::allocator_arg, alloc_, std::move(other));
      xyz_protocol_move_from<false>(moved);
    }

    return *this;
  }

  // Swapping can move the owned objects, so, like move construction and
  // assignment, it requires a move constructible interface.
  constexpr void swap(protocol& other) noexcept(always_equal || pocs)
    requires std::is_move_constructible_v<I>
  {
    if (this == std::addressof(other)) {
      return;
    }

    if constexpr (!always_equal && !pocs) {
      // The behavior is undefined if the allocators are not equal.
      assert(alloc_ == other.alloc_ &&
             "allocators must compare equal or propagate on swap");
    }

    detail::small_buffer_storage storage;
    vtable_->relocate(storage_, storage);
    other.vtable_->relocate(other.storage_, storage_);
    vtable_->relocate(storage, other.storage_);

    using std::swap;
    if constexpr (pocs) {
      swap(alloc_, other.alloc_);
    }
    swap(vtable_, other.vtable_);
  }

  // Can be discovered by ADL for more optimal swapping than std::swap.
  friend constexpr void swap(protocol& lhs,
                             protocol& rhs) noexcept(always_equal || pocs)
    requires std::is_move_constructible_v<I>
  {
    return lhs.swap(rhs);
  }

  constexpr const Alloc& get_allocator() const { return alloc_; }

  // Hidden friend rather than a member so it can't collide with a forwarded
  // interface member function of the same name.
  friend constexpr bool valueless_after_move(const protocol& p) noexcept
    requires std::is_move_constructible_v<I>
  {
    return p.vtable_ == &null_vtable;
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
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  protocol_view(U& object)
      : object_(static_cast<void*>(std::addressof(object))),
        vtable_(&detail::view_vtable_for<T, U>) {}

  // A view of a temporary would dangle.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  protocol_view(const U&&) = delete;

  // Views the object a `protocol<T>` owns.
  //
  // Precondition: `p` is not valueless.
  //
  // Moving, swapping or assigning to `p` invalidates the view if the object is
  // stored in `p`'s small buffer.
  template <typename Alloc>
  protocol_view(protocol<T, Alloc>& p) noexcept
      : object_(p.xyz_protocol_object_address()),
        vtable_(p.vtable_->object_vtable) {}

  // A view of a temporary would dangle.
  template <typename Alloc>
  protocol_view(protocol<T, Alloc>&&) = delete;

 private:
  // Grants `protocol_view<const T>` access so it can share this view's
  // `object_`/`vtable_`.
  template <detail::is_valid_view_interface>
  friend class protocol_view;

  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::operator_thunk;

  template <typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::conversion_thunk;

  template <typename U>
    requires(is_protocol_conformant_v<T, std::decay_t<U>>)
  friend constexpr U& protocol_cast(protocol_view operand) {
    if (*operand.vtable_->xyz_protocol_typeid != typeid(U)) {
      throw bad_protocol_cast{};
    }

    return *std::launder(static_cast<std::decay_t<U>*>(operand.object_));
  }

  template <typename U>
    requires(is_protocol_conformant_v<T, U>)
  friend constexpr U* protocol_cast(protocol_view* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(U)) {
      return nullptr;
    }

    return std::launder(static_cast<U*>(operand->object_));
  }

  friend constexpr const std::type_info& target_type(protocol_view p) noexcept {
    return *p.vtable_->xyz_protocol_typeid;
  }

  // The pointer the synthesised member thunks pass to the vtable entries. The
  // view's const is shallow, so a const view passes a mutable pointer.
  // Private and prefixed, as for `protocol`.
  constexpr void* xyz_protocol_erased_object() const noexcept {
    return object_;
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
template <detail::is_valid_interface T>
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
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>)
  protocol_view(const U& object)
      : object_(static_cast<const void*>(std::addressof(object))),
        vtable_(&detail::view_vtable_for<T, U>) {}

  // A view of a temporary would dangle.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!detail::is_protocol_view_v<std::remove_cvref_t<U>>)
  protocol_view(const U&&) = delete;

  // Views the object a `protocol<T>` owns.
  //
  // Precondition: `p` is not valueless.
  //
  // Moving, swapping or assigning to `p` invalidates the view if the object is
  // stored in `p`'s small buffer.
  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>& p) noexcept
      : object_(p.xyz_protocol_object_address()),
        vtable_(p.vtable_->object_vtable) {}

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
      : object_(view.object_), vtable_(view.vtable_) {}

 private:
  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::member_function_thunk;

  template <std::meta::operators Operator, typename FnPtrType,
            typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::operator_thunk;

  template <typename ProtocolType, typename Vtable, std::meta::info Member,
            detail::member_qualifiers MemberQualifiers>
  friend struct detail::conversion_thunk;

  template <typename U>
    requires(is_protocol_conformant_v<T, std::decay_t<U>>)
  friend constexpr const U& protocol_cast(protocol_view operand) {
    if (*operand.vtable_->xyz_protocol_typeid != typeid(U)) {
      throw bad_protocol_cast{};
    }

    return *std::launder(static_cast<const std::decay_t<U>*>(operand.object_));
  }

  template <typename U>
    requires(is_protocol_conformant_v<T, U>)
  friend constexpr const U* protocol_cast(protocol_view* operand) noexcept {
    if (*operand->vtable_->xyz_protocol_typeid != typeid(U)) {
      return nullptr;
    }

    return std::launder(static_cast<const U*>(operand->object_));
  }

  friend constexpr const std::type_info& target_type(protocol_view p) noexcept {
    return *p.vtable_->xyz_protocol_typeid;
  }

  // The pointer the synthesised member thunks pass to the vtable entries.
  // Private and prefixed, as for `protocol`.
  constexpr const void* xyz_protocol_erased_object() const noexcept {
    return object_;
  }

  // Non-owning pointer to the viewed object.
  const void* object_ = nullptr;

  const detail::vtable_t<T>* vtable_;
};

}  // namespace xyz

#endif  // XYZ_PROTOCOL_PROTOCOL_HH_

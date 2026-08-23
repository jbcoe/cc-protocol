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

#include <algorithm>
#include <cassert>
#include <concepts>
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
        return std::ranges::any_of(candidate_member_functions,
                                   [&](std::meta::info candidate_member) {
                                     return detail::member_function_conforms_to(
                                         candidate_member, interface_member);
                                   });
      });
}

// Variable template for use in requires clauses.
template <typename Interface, typename Candidate>
inline constexpr bool is_protocol_conformant_v =
    is_protocol_conformant<Interface, Candidate>();

template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol {
  using traits = std::allocator_traits<Alloc>;

  template <typename T>
  using rebound = traits::template rebind_alloc<T>;

  template <typename T>
  using rebound_traits = std::allocator_traits<rebound<T>>;

  constexpr static bool pocca =
      traits::propagate_on_container_copy_assignment::value;

  constexpr static bool pocma =
      traits::propagate_on_container_move_assignment::value;

  constexpr static bool pocs = traits::propagate_on_container_swap::value;

  constexpr static bool always_equal = traits::is_always_equal::value;

  template <typename T, typename TNorm = std::decay_t<T>, typename... Args>
  constexpr static TNorm* create(const Alloc& alloc, Args&&... args) {
    rebound<TNorm> new_alloc{alloc};

    auto obj = rebound_traits<TNorm>::allocate(new_alloc, 1);
    try {
      rebound_traits<TNorm>::construct(new_alloc, obj, std::forward<Args>(args)...);
    } catch (...) {
      rebound_traits<TNorm>::deallocate(new_alloc, obj, 1);
      throw;
    }

    return obj;
  }

  struct vtable {
    void (*destroy)(const Alloc& alloc, void* data);
    void* (*copy)(const Alloc& alloc, const void* data);
    void* (*move)(const Alloc& alloc, void* data);
  };

  template <typename T, typename TNorm = std::decay_t<T>>
  constexpr static vtable vtable_for = {
      .destroy = +[](const Alloc& alloc, void* data) -> void {
        rebound<TNorm> new_alloc{alloc};
        auto* typed = static_cast<TNorm*>(data);
        rebound_traits<TNorm>::destroy(new_alloc, typed);
        rebound_traits<TNorm>::deallocate(new_alloc, typed, 1);
      },

      .copy = +[](const Alloc& alloc, const void* data) -> void* {
        return create<TNorm>(alloc, *static_cast<const TNorm*>(data));
      },

      .move = +[](const Alloc& alloc, void* data) -> void* {
        return create<TNorm>(alloc, std::move(*static_cast<TNorm*>(data)));
      }};

  constexpr static vtable null_vtable = {
      .destroy = +[](const Alloc&, void*) -> void {},
      .copy = +[](const Alloc&, const void*) -> void* { return nullptr; },
      .move = +[](const Alloc&, void*) -> void* { return nullptr; }};

  [[no_unique_address]] Alloc alloc_;

  void* obj_ = nullptr;
  const vtable* vtable_ = &null_vtable;

 public:
  using allocator_type = Alloc;

  protocol() = delete;

  template <typename T>
    requires (!std::same_as<std::decay_t<T>, protocol> && detail::meets_interface<T, I> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(T && obj)
      : protocol(std::allocator_arg, Alloc{}, std::forward<T>(obj)) {}

  template <typename T>
    requires (!std::same_as<std::decay_t<T>, protocol> && detail::meets_interface<T, I>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a),
        obj_(create<T>(alloc_, std::forward<T>(obj))),
        vtable_(&vtable_for<T>) {}

  constexpr ~protocol() { vtable_->destroy(alloc_, obj_); }

  constexpr protocol(const protocol& other)
    requires std::is_copy_constructible_v<I>
      : protocol(std::allocator_arg,
                 traits::select_on_container_copy_construction(other.alloc_),
                 other) {}

  constexpr protocol(std::allocator_arg_t, const Alloc& a, const protocol& other)
    requires std::is_copy_constructible_v<I>
      : alloc_(a),
        obj_(other.vtable_->copy(alloc_, other.obj_)),
        vtable_(other.vtable_) {}

  constexpr protocol(protocol&& other) noexcept
      : protocol(std::allocator_arg, other.alloc_, std::move(other)) {}

  constexpr protocol(std::allocator_arg_t, const Alloc& a,
           protocol&& other) noexcept(always_equal)
      : alloc_(a), vtable_(other.vtable_) {
    if (always_equal || alloc_ == other.alloc_) {
      obj_ = other.obj_;
    } else {
      obj_ = other.vtable_->move(alloc_, other.obj_);
      other.vtable_->destroy(other.alloc_, other.obj_);
    }

    other.obj_ = nullptr;
    other.vtable_ = &null_vtable;
  }

  constexpr protocol& operator=(const protocol& other)
    requires std::is_copy_constructible_v<I>
  {
    if (this == &other) {
      return *this;
    }

    if constexpr (pocca) {
      void* new_obj = other.vtable_->copy(other.alloc_, other.obj_);

      vtable_->destroy(alloc_, obj_);
      obj_ = new_obj;
      alloc_ = other.alloc_;
    } else {
      void* new_obj = other.vtable_->copy(alloc_, other.obj_);
      vtable_->destroy(alloc_, obj_);
      obj_ = new_obj;
    }
    vtable_ = other.vtable_;

    return *this;
  }

  constexpr protocol& operator=(protocol&& other) noexcept(always_equal || pocma) {
    if (this == &other) {
      return *this;
    }

    if (always_equal || pocma || alloc_ == other.alloc_) {
      vtable_->destroy(alloc_, obj_);
      obj_ = other.obj_;
      if constexpr (pocma) {
        alloc_ = other.alloc_;
      }
    } else {
      void* new_obj = other.vtable_->move(alloc_, other.obj_);
      vtable_->destroy(alloc_, obj_);
      other.vtable_->destroy(other.alloc_, other.obj_);

      obj_ = new_obj;
    }

    other.obj_ = nullptr;
    vtable_ = std::exchange(other.vtable_, &null_vtable);

    return *this;
  }

  constexpr void swap(protocol& other) noexcept(always_equal || pocs) {
    if constexpr (!always_equal && !pocs) {
      assert(alloc_ == other.alloc_);
    }

    using std::swap;
    if constexpr (pocs) {
      swap(alloc_, other.alloc_);
    }
    swap(obj_, other.obj_);
    swap(vtable_, other.vtable_);
  }

  friend constexpr void swap(protocol& lhs, protocol& rhs) noexcept(always_equal || pocs) {
    return lhs.swap(rhs);
  }

  constexpr const Alloc& get_allocator() const { return alloc_; }

  constexpr bool valueless_after_move() const {
    return obj_ == nullptr;
  }
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
#endif  // XYZ_REFLECTION_PROTOCOL_HH_

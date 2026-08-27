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
#include <cassert>
#include <concepts>
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
  // Parameter counts must match.
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
// TODO(jbcoe): Handle static functions as they can be used to satisfy interface
// conformance.
template <std::meta::info Type>
consteval auto protocol_interface_functions_of() {
  return std::define_static_array(
      std::define_static_array(
          members_of(Type, std::meta::access_context::unprivileged())) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::not_fn(std::meta::is_static_member)) |
      std::views::filter(std::meta::has_identifier));
}

// Vanishing-this-pointer thunk for a synthesised member stub. The thunk
// carries a single operator() whose signature mirrors one method of the
// Interface type.
template <typename FnPtrType, typename EnclosingType, bool IsConst,
          bool IsNoexcept>
struct method_thunk;

// TODO(jbcoe): Extend this approach to handle lvalue and rvalue qualifiers.
template <typename R, typename... Args, typename EnclosingType, bool IsConst,
          bool IsNoexcept>
struct method_thunk<R (*)(Args...), EnclosingType, IsConst, IsNoexcept> {
  R operator()(Args... /*args*/) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    [[maybe_unused]] auto* base = reinterpret_cast<EnclosingType*>(this);
    std::unreachable();  // vtable dispatch not yet implemented
  }

  R operator()(Args... /*args*/) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    [[maybe_unused]] const auto* base =
        reinterpret_cast<const EnclosingType*>(this);
    std::unreachable();  // vtable dispatch not yet implemented
  }
};

template <typename R, typename... Args>
using fn_ptr_t = R (*)(Args...);

// A single-member base wrapping the thunk for one interface member function,
// named after that method (giving the `p.method_name(args)` call syntax).
template <std::meta::info Member>
struct member_base_generator {
  struct member_base;
  consteval {
    std::string_view name = identifier_of(Member);

    // Build the function-pointer type R(*)(Args...) from the method's
    // return type and parameter types.
    std::vector<std::meta::info> fn_args{dealias(return_type_of(Member))};
    std::vector<std::meta::info> member_parameters = parameters_of(Member);
    fn_args.append_range(member_parameters |
                         std::views::transform(std::meta::type_of));
    std::meta::info fn_ptr_type = substitute(^^fn_ptr_t, fn_args);

    // clang-format off
    std::meta::info thunk_type = substitute(
        ^^method_thunk, {fn_ptr_type, ^^member_base,
                       std::meta::reflect_constant(is_const(Member)),
                       std::meta::reflect_constant(is_noexcept(Member))});
    // clang-format on

    define_aggregate(
        ^^member_base,
        {
            data_member_spec(thunk_type,
                             std::meta::data_member_options{
                                 .name = name, .no_unique_address = true})});
  }
};

// Combines the single-member base types produced by `member_base_generator`
// into one type via multiple inheritance.
template <typename... MemberBases>
struct stub_bases : MemberBases... {};

// Returns a `stub_bases` specialisation with one base per public, non-special,
// member function of `interface_type`, giving named members with `operator()`
// for each.
//
// Two bases defining a member of the same name make that name ambiguous to
// look up through the derived class, so overloaded methods are unsupported
// for now.
template <std::meta::info InterfaceType>
consteval std::meta::info generate_stub_bases() {
  std::vector<std::meta::info> member_base_types;

  template for (constexpr std::meta::info member : std::define_static_array(
                    std::define_static_array(
                        members_of(InterfaceType,
                                   std::meta::access_context::unprivileged())) |
                    std::views::filter(std::meta::is_function) |
                    std::views::filter(
                        std::not_fn(std::meta::is_special_member_function)) |
                    std::views::filter(
                        std::not_fn(std::meta::is_static_member)) |
                    std::views::filter(std::meta::has_identifier))) {
    member_base_types.push_back(
        ^^typename member_base_generator<member>::member_base);
  }
  return substitute(^^stub_bases, member_base_types);
}

// The generated stub type for `T`: a `stub_bases` specialisation with named
// members with `operator()` for each public, non-special, member function
// from `T`.
template <typename T>
using protocol_stubs_t = typename[:generate_stub_bases<^^T>():];

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

  // Checking for protocol interface conformance is O(N*M) over member counts,
  // assumed to be negligible at compile time.
  // TODO(jbcoe): Use set/map once there is library support for `constexpr`.
  auto interface_member_functions =
      detail::protocol_interface_functions_of<^^Interface>();
  auto candidate_member_functions =
      detail::protocol_interface_functions_of<^^Candidate>();

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

// ---------------------------------------------------------------------------
// protocol<I, Allocator>
// ---------------------------------------------------------------------------
template <typename I, typename Alloc = std::allocator<std::byte>>
class protocol : public detail::protocol_stubs_t<I> {
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

  // Stores necessary functions for rule-of-five implementation.
  // NOTE: This should be extended/unified with the view vtable.
  // Maybe the owning vtable can inherit from the view one?
  struct vtable {
    void (*destroy)(const Alloc& alloc, void* data);
    void* (*copy)(const Alloc& alloc, const void* data);
    void* (*move)(const Alloc& alloc, void* data);
  };

  // Creates a vtable for the type T. TNorm is used throughout
  // this file to create a convenient alias for a decayed type.
  template <typename T, typename TNorm = std::decay_t<T>>
  static constexpr vtable vtable_for = {
      .destroy = +[](const Alloc& alloc, void* data) -> void {
        rebound<TNorm> new_alloc{alloc};
        auto* typed = static_cast<TNorm*>(data);
        rebound_traits<TNorm>::destroy(new_alloc, typed);
        rebound_traits<TNorm>::deallocate(new_alloc, typed, 1);
      },

      // Copy construction and assignment should only reach this
      // if the interface is copy constructible.
      .copy = +[](const Alloc& alloc, const void* data) -> void* {
        if constexpr (std::is_copy_constructible_v<I>) {
          return create<TNorm>(alloc, *static_cast<const TNorm*>(data));
        } else {
          std::unreachable();
        }
      },

      .move = +[](const Alloc& alloc, void* data) -> void* {
        return create<TNorm>(alloc, std::move(*static_cast<TNorm*>(data)));
      }};

  // A no-op vtable that is the stand-in for a nullptr vtable. Prevents
  // redundant null checks throughout the code.
  static constexpr vtable null_vtable = {
      .destroy = +[](const Alloc&, void*) -> void {},
      .copy = +[](const Alloc&, const void*) -> void* { return nullptr; },
      .move = +[](const Alloc&, void*) -> void* { return nullptr; }};

  [[no_unique_address]] Alloc alloc_;

  void* obj_ = nullptr;
  const vtable* vtable_ = &null_vtable;

 public:
  using allocator_type = Alloc;

  protocol() = delete;

  // Construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             is_protocol_conformant_v<I, TNorm> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(T&& obj)
      : protocol(std::allocator_arg, Alloc{}, std::forward<T>(obj)) {}

  // Allocator-aware construction from conforming T.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, protocol> &&
             is_protocol_conformant_v<I, TNorm>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a),
        obj_(create<T>(alloc_, std::forward<T>(obj))),
        vtable_(&vtable_for<T>) {}

  // In-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, std::forward<Args>(args)...) {}

  // Allocator-aware in-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>, Args&&... args)
      : alloc_(a),
        obj_(create<T>(alloc_, std::forward<Args>(args)...)),
        vtable_(&vtable_for<T>) {}

  // In-place construction needs this overload because templates cannot
  // deduce initializer lists.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, il, std::forward<Args>(args)...) {
  }

  // Allocator-aware in-place init-list construction from conforming T.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : alloc_(a),
        obj_(create<T>(alloc_, il, std::forward<Args>(args)...)),
        vtable_(&vtable_for<T>) {}

  constexpr ~protocol() { vtable_->destroy(alloc_, obj_); }

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
        obj_(other.vtable_->copy(alloc_, other.obj_)),
        vtable_(other.vtable_) {}

  // Move construction.
  constexpr protocol(protocol&& other) noexcept
      : protocol(std::allocator_arg, other.alloc_, std::move(other)) {}

  // Allocator-aware move construction.
  constexpr protocol(std::allocator_arg_t, const Alloc& a,
                     protocol&& other) noexcept(always_equal)
      : alloc_(a), vtable_(other.vtable_) {
    if (always_equal || alloc_ == other.alloc_) {
      // Fast path, we can just do a pointer swap.
      obj_ = other.obj_;
    } else {
      // Slow path, we have to heap allocate and move construct.
      obj_ = other.vtable_->move(alloc_, other.obj_);
      other.vtable_->destroy(other.alloc_, other.obj_);
    }

    other.obj_ = nullptr;
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

  // Move assignment.
  constexpr protocol& operator=(protocol&& other) noexcept(always_equal ||
                                                           pocma) {
    if (this == &other) {
      return *this;
    }

    if (always_equal || pocma || alloc_ == other.alloc_) {
      // Fast path: just swap the pointers and (conditionally) the allocators.
      vtable_->destroy(alloc_, obj_);
      obj_ = other.obj_;
      if constexpr (pocma) {
        alloc_ = other.alloc_;
      }
    } else {
      // Slow path: heap construct and move the object directly. Allocate first
      // for strong exception safety.
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
      // The behavior is undefined if the allocators are not equal.
      assert(alloc_ == other.alloc_ &&
             "allocators must compare equal or propagate on swap");
    }

    using std::swap;
    if constexpr (pocs) {
      swap(alloc_, other.alloc_);
    }
    swap(obj_, other.obj_);
    swap(vtable_, other.vtable_);
  }

  // Can be discovered by ADL for more optimal swapping than std::swap.
  friend constexpr void swap(protocol& lhs,
                             protocol& rhs) noexcept(always_equal || pocs) {
    return lhs.swap(rhs);
  }

  constexpr const Alloc& get_allocator() const { return alloc_; }

  constexpr bool valueless_after_move() const { return obj_ == nullptr; }
};

template <typename T>
class protocol_view : public detail::protocol_stubs_t<T> {
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

  // Construct from any type U that conforms to the Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_view_v<std::remove_cvref_t<U>>)
  explicit protocol_view(const U& object);
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_HH_

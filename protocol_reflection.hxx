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
// The C++26-reflection backend: a single xyz::protocol<T, Allocator> class
// template body that works for any interface satisfying
// reflection_protocol_concept, with no per-interface code generation step.
//
// Allocator-aware construction, copy/move/destroy/swap, and dispatch, one
// interface at a time. No narrowing conversions between protocol<T,...>
// specializations yet. xyz::protocol_view is out of scope here; it is
// added once dispatch is proven.
#ifndef XYZ_PROTOCOL_REFLECTION_HXX_
#define XYZ_PROTOCOL_REFLECTION_HXX_

#include <memory>
#include <meta>
#include <utility>
#include <vector>

#include "protocol_reflection_detail/conformance.hxx"
#include "protocol_reflection_detail/forwarders.hxx"
#include "protocol_reflection_detail/members.hxx"
#include "protocol_reflection_detail/naming.hxx"
#include "protocol_reflection_detail/thunk.hxx"
#include "protocol_reflection_detail/types.hxx"
#include "protocol_reflection_detail/vtable_layout.hxx"

namespace xyz {
namespace reflection_detail {

// Every member of `interface` sharing `member`'s own name, in declaration
// order: the sibling overloads a forwarder's single named data member has
// to merge into one callable (e.g. interface C's overloaded `compute`s).
consteval std::vector<std::meta::info> members_named_like(
    std::meta::info interface, std::meta::info member) {
  std::string_view name = std::meta::identifier_of(member);
  std::vector<std::meta::info> result;
  for (std::meta::info candidate : interface_member_functions(interface)) {
    if (std::meta::has_identifier(candidate) &&
        std::meta::identifier_of(candidate) == name) {
      result.push_back(candidate);
    }
  }
  return result;
}

// A concrete, non-generic forwarder for one interface overload: its
// operator() has that overload's own parameter types rather than a
// forwarding template, so overload resolution across several such
// forwarders (protocol_member_wrapper_combinator) ranks them the way it
// would rank the interface's own overloads. Same reason
// conformance.hxx's single_candidate_forwarder is concrete, one level up:
// there it merges an implementation's candidates for one interface member;
// here it merges an interface's own overloads sharing one name.
template <typename Interface, typename Allocator, std::meta::info Member,
          typename R, typename... Ps>
struct protocol_single_overload_wrapper {
  R operator()(Ps... ps) const noexcept(std::meta::is_noexcept(Member));
};

// Merges N single-overload forwarders into one callable type via
// `using Bases::operator()...`, exactly conformance.hxx's
// candidate_overload_set technique, applied to an interface's own
// overloads instead of an implementation's candidates.
template <typename... Bases>
struct protocol_member_wrapper_combinator : Bases... {
  using Bases::operator()...;
};

// The merged forwarder type for the whole name-group `member` belongs to
// (a single overload if `member` isn't overloaded). A plain function, not
// a function template keyed only on Member: every member in the group
// produces the same result, so protocol_single_overload_wrapper's
// operator() can recompute it from just its own Member, without
// protocol_bases_type threading the whole group through separately.
consteval std::meta::info protocol_member_wrapper_type(
    std::meta::info interface, std::meta::info allocator,
    std::meta::info member) {
  std::vector<std::meta::info> base_types;
  for (std::meta::info sibling : members_named_like(interface, member)) {
    std::vector<std::meta::info> args{
        interface, allocator, std::meta::reflect_constant(sibling),
        std::meta::dealias(std::meta::return_type_of(sibling))};
    for (std::meta::info parameter_type : parameter_types_of(sibling)) {
      args.push_back(parameter_type);
    }
    base_types.push_back(
        std::meta::substitute(^^protocol_single_overload_wrapper, args));
  }
  return std::meta::substitute(^^protocol_member_wrapper_combinator,
                               base_types);
}

template <typename Interface, typename Allocator, std::meta::info Member>
using protocol_member_wrapper =
    typename[:protocol_member_wrapper_type(^^Interface, ^^Allocator, Member):];

template <typename Interface, typename Allocator>
consteval std::meta::info protocol_bases_type() {
  std::vector<std::meta::info> bases;
  for (std::meta::info member : interface_member_functions(^^Interface)) {
    // Every member in a name-group produces the same
    // members_named_like(...)[0]; only process the group once, at its
    // first member, so an overloaded name doesn't get a forwarder_base
    // (and hence a same-named data member) built once per overload.
    std::meta::info representative = members_named_like(^^Interface, member)[0];
    if (representative != member) {
      continue;
    }
    std::meta::info wrapper_type =
        protocol_member_wrapper_type(^^Interface, ^^Allocator, member);
    bases.push_back(std::meta::substitute(
        ^^forwarder_base,
        {
            wrapper_type, std::meta::reflect_constant(representative)}));
  }
  return forwarders_type(bases);
}

// protocol<Interface, Allocator>'s own base list: one forwarder_base per
// distinct member name, combined via forwarders.hxx's combinator. protocol
// inherits this directly, not through an intermediate type, so each
// Wrapper's base-to-derived static_cast to protocol<Interface, Allocator>
// is valid once protocol is complete.
template <typename Interface, typename Allocator>
using protocol_bases = typename[:protocol_bases_type<Interface, Allocator>():];

}  // namespace reflection_detail

// protocol<Interface, Allocator>'s own nested vtable already is the owning
// vtable used for narrowing; this trait just names it the way
// protocol.h's get_owning_vtable expects, matching how the Python/libclang
// backend's own generated protocol_owning_vtable_traits<::xyz::A,
// Allocator> is a bare alias to protocol<::xyz::A, Allocator>::vtable, not
// a separately-designed type.
template <typename Interface, typename Allocator>
struct protocol_owning_vtable_traits {
  using vtable = typename protocol<Interface, Allocator>::vtable;
};

// Narrows a source owning vtable to a target one by copying every entry
// the target has (by exact name -- including xyz_protocol_clone/_move/
// _destroy, which every protocol's own vtable always declares) from the
// matching entry in the source. FromVtable/ToVtable are deduced directly
// from the pointer arguments, not from an Interface/Allocator template
// argument list: unlike the Python/libclang backend, where a per-interface
// generated overload hardcodes its own target type, this one generic
// definition works for any (FromInterface, ToInterface, Allocator), since
// it reflects on the two concrete vtable struct types themselves rather
// than needing to know which interfaces they came from. Found via ADL
// from get_owning_vtable (protocol.h): FromVtable/ToVtable are nested
// types of xyz::protocol<...>, so this must live in namespace xyz, not
// xyz::reflection_detail, to be visible there.
template <typename FromVtable, typename ToVtable>
void map_owning_vtable_members(const FromVtable* from, ToVtable* to) {
  template for (constexpr std::meta::info to_member :
                std::define_static_array(std::meta::nonstatic_data_members_of(
                    ^^ToVtable, std::meta::access_context::current()))) {
    constexpr std::string_view name = std::meta::identifier_of(to_member);
    constexpr std::meta::info from_member =
        reflection_detail::find_data_member(^^FromVtable, name);
    to->[:to_member:] = from->[:from_member:];
  }
}

template <typename T, typename Allocator>
class protocol : public reflection_detail::protocol_bases<T, Allocator> {
  template <typename, typename, std::meta::info, typename, typename...>
  friend struct reflection_detail::protocol_single_overload_wrapper;
  template <typename, typename>
  friend class protocol;

  using clone_or_move_fn = void* (*)(void*, const Allocator&);
  using destroy_fn = void (*)(void*, const Allocator&);

  // protocol's own vtable: clone/move/destroy, plus one entry per
  // interface member, all erased through void* (never const void*).
  // The owning object itself provides both const and non-const access
  // paths, so a single erased pointer kind suffices here, unlike
  // vtable_layout.hxx's view_vtable/const_view_vtable split.
  struct vtable_incomplete;
  consteval {
    std::vector<std::meta::info> specs = {
        std::meta::data_member_spec(^^clone_or_move_fn,
                                    {
                                        .name = "xyz_protocol_clone"}),
        std::meta::data_member_spec(^^clone_or_move_fn,
                                    {
                                        .name = "xyz_protocol_move"}),
        std::meta::data_member_spec(^^destroy_fn,
                                    {
                                        .name = "xyz_protocol_destroy"}),
    };
    for (std::meta::info spec :
         reflection_detail::define_vtable_entries(^^T, ^^void*, false)) {
      specs.push_back(spec);
    }
    define_aggregate(^^vtable_incomplete, specs);
  }

 public:
  using vtable = vtable_incomplete;

 private:
  template <typename Implementation>
  struct vtable_impl {
    using t_allocator = typename std::allocator_traits<
        Allocator>::template rebind_alloc<Implementation>;
    using t_alloc_traits = std::allocator_traits<t_allocator>;

    static void* xyz_protocol_clone(void* cb, const Allocator& alloc) {
      auto* self = static_cast<Implementation*>(cb);
      t_allocator t_alloc(alloc);
      auto mem = t_alloc_traits::allocate(t_alloc, 1);
      try {
        t_alloc_traits::construct(t_alloc, mem, *self);
        return mem;
      } catch (...) {
        t_alloc_traits::deallocate(t_alloc, mem, 1);
        throw;
      }
    }

    static void* xyz_protocol_move(void* cb, const Allocator& alloc) {
      auto* self = static_cast<Implementation*>(cb);
      t_allocator t_alloc(alloc);
      auto mem = t_alloc_traits::allocate(t_alloc, 1);
      try {
        t_alloc_traits::construct(t_alloc, mem, std::move(*self));
        return mem;
      } catch (...) {
        t_alloc_traits::deallocate(t_alloc, mem, 1);
        throw;
      }
    }

    static void xyz_protocol_destroy(void* cb, const Allocator& alloc) {
      auto* self = static_cast<Implementation*>(cb);
      t_allocator t_alloc(alloc);
      t_alloc_traits::destroy(t_alloc, self);
      t_alloc_traits::deallocate(t_alloc, self, 1);
    }

    static consteval vtable build() {
      vtable vt{};
      vt.xyz_protocol_clone = xyz_protocol_clone;
      vt.xyz_protocol_move = xyz_protocol_move;
      vt.xyz_protocol_destroy = xyz_protocol_destroy;
      template for (constexpr std::meta::info member : std::define_static_array(
                        reflection_detail::interface_member_functions(^^T))) {
        constexpr std::meta::info entry = reflection_detail::find_data_member(
            ^^vtable, reflection_detail::vtable_slot_name(member));
        constexpr std::meta::info merged_type =
            reflection_detail::candidate_overload_set_type(
                reflection_detail::resolve_implementation_candidates(
                    member, ^^Implementation),
                ^^Implementation&);
        using Thunk = reflection_detail::erased_call_thunk<
            Implementation,
            typename[:merged_type:], typename
                      [:reflection_detail::member_function_type(
                            member):], false, std::meta::is_noexcept(member)>;
        vt.[:entry:] = &Thunk::call;
      }
      return vt;
    }

    static constexpr vtable vtable_ = build();
  };

  template <typename U, typename... Ts>
  [[nodiscard]] void* create_storage(Ts&&... ts) const {
    using t_allocator =
        typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
    t_allocator t_alloc(alloc_);
    using t_alloc_traits = std::allocator_traits<t_allocator>;
    auto mem = t_alloc_traits::allocate(t_alloc, 1);
    try {
      t_alloc_traits::construct(t_alloc, mem, std::forward<Ts>(ts)...);
      return mem;
    } catch (...) {
      t_alloc_traits::deallocate(t_alloc, mem, 1);
      throw;
    }
  }

  void* p_;
  const vtable* vtable_;
  [[no_unique_address]] Allocator alloc_;

  using allocator_traits = std::allocator_traits<Allocator>;

 public:
  template <class U, class... Ts>
  explicit constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                              std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> && std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> &&
             reflection_detail::reflection_protocol_concept<U, T>
      : alloc_(alloc) {
    p_ = create_storage<U>(std::forward<Ts>(ts)...);
    vtable_ = &vtable_impl<U>::vtable_;
  }

  template <class U, class... Ts>
  explicit constexpr protocol(std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> && std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> &&
             std::default_initializable<Allocator> &&
             reflection_detail::reflection_protocol_concept<U, T>
      : protocol(std::allocator_arg_t{}, Allocator{}, std::in_place_type<U>,
                 std::forward<Ts>(ts)...) {}

  constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                     const protocol& other)
      : alloc_(alloc) {
    if (!other.valueless_after_move()) {
      p_ = other.vtable_->xyz_protocol_clone(other.p_, alloc_);
      vtable_ = other.vtable_;
    } else {
      p_ = nullptr;
      vtable_ = nullptr;
    }
  }

  constexpr protocol(const protocol& other)
      : protocol(std::allocator_arg_t{},
                 allocator_traits::select_on_container_copy_construction(
                     other.alloc_),
                 other) {}

  constexpr protocol(
      std::allocator_arg_t, const Allocator& alloc,
      protocol&& other) noexcept(allocator_traits::is_always_equal::value)
      : alloc_(alloc) {
    if constexpr (allocator_traits::is_always_equal::value) {
      p_ = std::exchange(other.p_, nullptr);
      vtable_ = std::exchange(other.vtable_, nullptr);
    } else {
      if (alloc_ == other.alloc_) {
        p_ = std::exchange(other.p_, nullptr);
        vtable_ = std::exchange(other.vtable_, nullptr);
      } else {
        if (!other.valueless_after_move()) {
          p_ = other.vtable_->xyz_protocol_move(other.p_, alloc_);
          vtable_ = other.vtable_;
          other.vtable_->xyz_protocol_destroy(other.p_, other.alloc_);
          other.p_ = nullptr;
          other.vtable_ = nullptr;
        } else {
          p_ = nullptr;
          vtable_ = nullptr;
        }
      }
    }
  }

  constexpr protocol(protocol&& other) noexcept(
      allocator_traits::is_always_equal::value)
      : protocol(std::allocator_arg_t{}, other.alloc_, std::move(other)) {}

  template <typename Other>
    requires(!std::same_as<Other, T>)
  constexpr protocol(std::allocator_arg_t, const Allocator& alloc,
                     const protocol<Other, Allocator>& other)
      : alloc_(alloc) {
    if (!other.valueless_after_move()) {
      p_ = other.vtable_->xyz_protocol_clone(other.p_, alloc_);
      vtable_ = get_owning_vtable<Other, T, Allocator>(other.vtable_);
    } else {
      p_ = nullptr;
      vtable_ = nullptr;
    }
  }

  template <typename Other>
    requires(!std::same_as<Other, T>)
  constexpr protocol(const protocol<Other, Allocator>& other)
      : alloc_(allocator_traits::select_on_container_copy_construction(
            other.alloc_)) {
    if (!other.valueless_after_move()) {
      p_ = other.vtable_->xyz_protocol_clone(other.p_, alloc_);
      vtable_ = get_owning_vtable<Other, T, Allocator>(other.vtable_);
    } else {
      p_ = nullptr;
      vtable_ = nullptr;
    }
  }

  template <typename Other>
    requires(!std::same_as<Other, T>)
  constexpr protocol(
      std::allocator_arg_t, const Allocator& alloc,
      protocol<Other, Allocator>&&
          other) noexcept(allocator_traits::is_always_equal::value)
      : alloc_(alloc) {
    if (alloc_ == other.alloc_) {
      p_ = std::exchange(other.p_, nullptr);
      vtable_ = get_owning_vtable<Other, T, Allocator>(
          std::exchange(other.vtable_, nullptr));
    } else {
      if (!other.valueless_after_move()) {
        p_ = other.vtable_->xyz_protocol_move(other.p_, alloc_);
        vtable_ = get_owning_vtable<Other, T, Allocator>(other.vtable_);
        other.vtable_->xyz_protocol_destroy(other.p_, other.alloc_);
        other.p_ = nullptr;
        other.vtable_ = nullptr;
      } else {
        p_ = nullptr;
        vtable_ = nullptr;
      }
    }
  }

  template <typename Other>
    requires(!std::same_as<Other, T>)
  constexpr protocol(protocol<Other, Allocator>&& other) noexcept(
      allocator_traits::is_always_equal::value)
      : alloc_(other.alloc_) {
    if (alloc_ == other.alloc_) {
      p_ = std::exchange(other.p_, nullptr);
      vtable_ = get_owning_vtable<Other, T, Allocator>(
          std::exchange(other.vtable_, nullptr));
    } else {
      if (!other.valueless_after_move()) {
        p_ = other.vtable_->xyz_protocol_move(other.p_, alloc_);
        vtable_ = get_owning_vtable<Other, T, Allocator>(other.vtable_);
        other.vtable_->xyz_protocol_destroy(other.p_, other.alloc_);
        other.p_ = nullptr;
        other.vtable_ = nullptr;
      } else {
        p_ = nullptr;
        vtable_ = nullptr;
      }
    }
  }

  constexpr bool valueless_after_move() const noexcept { return p_ == nullptr; }

  ~protocol() {
    if (p_ != nullptr) {
      vtable_->xyz_protocol_destroy(p_, alloc_);
    }
  }

  protocol& operator=(protocol other) noexcept(
      allocator_traits::is_always_equal::value) {
    std::swap(p_, other.p_);
    std::swap(vtable_, other.vtable_);
    if constexpr (!allocator_traits::is_always_equal::value) {
      std::swap(alloc_, other.alloc_);
    }
    return *this;
  }

  void swap(protocol& other) noexcept(
      allocator_traits::is_always_equal::value) {
    std::swap(p_, other.p_);
    std::swap(vtable_, other.vtable_);
    if constexpr (!allocator_traits::is_always_equal::value) {
      std::swap(alloc_, other.alloc_);
    }
  }

  friend void swap(protocol& lhs, protocol& rhs) noexcept(
      allocator_traits::is_always_equal::value) {
    lhs.swap(rhs);
  }
};

namespace reflection_detail {

template <typename Interface, typename Allocator, std::meta::info Member,
          typename R, typename... Ps>
R protocol_single_overload_wrapper<Interface, Allocator, Member, R,
                                   Ps...>::operator()(Ps... ps) const
    noexcept(std::meta::is_noexcept(Member)) {
  using Owner = protocol<Interface, Allocator>;
  using Combined = protocol_member_wrapper<Interface, Allocator, Member>;
  // The same representative every group member recomputes identically, so
  // this names the exact forwarder_base specialization Combined is wrapped
  // in as its sole member, regardless of which sibling overload this
  // wrapper is for.
  constexpr std::meta::info representative =
      members_named_like(^^Interface, Member)[0];
  using Base = forwarder_base<Combined, representative>;
  const auto* combined = static_cast<const Combined*>(this);
  const auto* base =
      static_cast<const Base*>(static_cast<const void*>(combined));
  const auto* owner = static_cast<const Owner*>(base);
  constexpr std::meta::info entry =
      find_data_member(^^typename Owner::vtable, vtable_slot_name(Member));
  return owner->vtable_->[:entry:](owner->p_, std::forward<Ps>(ps)...);
}

}  // namespace reflection_detail

}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_HXX_

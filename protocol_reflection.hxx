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
// The C++26-reflection backend: single xyz::protocol<T, Allocator> and
// xyz::protocol_view<T>/protocol_view<const T> class templates that work
// for any interface satisfying reflection_protocol_concept, with no
// per-interface code generation step. Allocator-aware construction,
// copy/move/destroy/swap, dispatch, and narrowing conversions between
// protocol<T,...>/protocol_view<T> specializations.
#ifndef XYZ_PROTOCOL_REFLECTION_HXX_
#define XYZ_PROTOCOL_REFLECTION_HXX_

#include <cassert>
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

// The first *const* member sharing `member`'s name: the const view's
// representative for a name-group, which can differ from
// members_named_like(...)[0] when a group mixes const and non-const
// overloads. Every const sibling recomputes this identically, the
// invariant protocol_const_view_bases_type relies on to agree on one
// representative per const name-group, the same way protocol_bases_type
// relies on members_named_like(...)[0] for the unfiltered case.
consteval std::meta::info first_const_member_named_like(
    std::meta::info interface, std::meta::info member) {
  for (std::meta::info sibling : members_named_like(interface, member)) {
    if (std::meta::is_const(sibling)) {
      return sibling;
    }
  }
  throw std::meta::exception("no const sibling", ^^void);
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

// protocol_view<Interface>/protocol_view<const Interface>'s own forwarder
// machinery: the same shape as protocol_single_overload_wrapper/
// protocol_member_wrapper_type/protocol_bases_type above, generalized to a
// different Owner shape. protocol_view has no Allocator, dispatches
// through vptr_/ptr_ instead of vtable_/p_, and (for the const view)
// exposes only const overloads. protocol_member_wrapper_combinator's
// merge-via-inheritance is Owner-agnostic, and reused as-is for both.

// Mutable protocol_view<Interface>: every member, dispatched through
// void*, exactly like protocol<Interface, Allocator>'s own entries.
template <typename Interface, std::meta::info Member, typename R,
          typename... Ps>
struct protocol_view_single_overload_wrapper {
  R operator()(Ps... ps) const noexcept(std::meta::is_noexcept(Member));
};

consteval std::meta::info protocol_view_member_wrapper_type(
    std::meta::info interface, std::meta::info member) {
  std::vector<std::meta::info> base_types;
  for (std::meta::info sibling : members_named_like(interface, member)) {
    std::vector<std::meta::info> args{
        interface, std::meta::reflect_constant(sibling),
        std::meta::dealias(std::meta::return_type_of(sibling))};
    for (std::meta::info parameter_type : parameter_types_of(sibling)) {
      args.push_back(parameter_type);
    }
    base_types.push_back(
        std::meta::substitute(^^protocol_view_single_overload_wrapper, args));
  }
  return std::meta::substitute(^^protocol_member_wrapper_combinator,
                               base_types);
}

template <typename Interface, std::meta::info Member>
using protocol_view_member_wrapper =
    typename[:protocol_view_member_wrapper_type(^^Interface, Member):];

template <typename Interface>
consteval std::meta::info protocol_view_bases_type() {
  std::vector<std::meta::info> bases;
  for (std::meta::info member : interface_member_functions(^^Interface)) {
    std::meta::info representative = members_named_like(^^Interface, member)[0];
    if (representative != member) {
      continue;
    }
    std::meta::info wrapper_type =
        protocol_view_member_wrapper_type(^^Interface, member);
    bases.push_back(std::meta::substitute(
        ^^forwarder_base,
        {
            wrapper_type, std::meta::reflect_constant(representative)}));
  }
  return forwarders_type(bases);
}

template <typename Interface>
using protocol_view_bases = typename[:protocol_view_bases_type<Interface>():];

// protocol_view<const Interface>: const members only, dispatched through
// const void*.
template <typename Interface, std::meta::info Member, typename R,
          typename... Ps>
struct protocol_const_view_single_overload_wrapper {
  R operator()(Ps... ps) const noexcept(std::meta::is_noexcept(Member));
};

consteval std::meta::info protocol_const_view_member_wrapper_type(
    std::meta::info interface, std::meta::info member) {
  std::vector<std::meta::info> base_types;
  for (std::meta::info sibling : members_named_like(interface, member)) {
    if (!std::meta::is_const(sibling)) {
      continue;
    }
    std::vector<std::meta::info> args{
        interface, std::meta::reflect_constant(sibling),
        std::meta::dealias(std::meta::return_type_of(sibling))};
    for (std::meta::info parameter_type : parameter_types_of(sibling)) {
      args.push_back(parameter_type);
    }
    base_types.push_back(std::meta::substitute(
        ^^protocol_const_view_single_overload_wrapper, args));
  }
  return std::meta::substitute(^^protocol_member_wrapper_combinator,
                               base_types);
}

template <typename Interface, std::meta::info Member>
using protocol_const_view_member_wrapper =
    typename[:protocol_const_view_member_wrapper_type(^^Interface, Member):];

template <typename Interface>
consteval std::meta::info protocol_const_view_bases_type() {
  std::vector<std::meta::info> bases;
  for (std::meta::info member : interface_member_functions(^^Interface)) {
    if (!std::meta::is_const(member)) {
      continue;
    }
    std::meta::info representative =
        first_const_member_named_like(^^Interface, member);
    if (representative != member) {
      continue;
    }
    std::meta::info wrapper_type =
        protocol_const_view_member_wrapper_type(^^Interface, member);
    bases.push_back(std::meta::substitute(
        ^^forwarder_base,
        {
            wrapper_type, std::meta::reflect_constant(representative)}));
  }
  return forwarders_type(bases);
}

template <typename Interface>
using protocol_const_view_bases =
    typename[:protocol_const_view_bases_type<Interface>():];

// Populates a const_view_vtable<Interface>/view_vtable<Interface>
// (vtable_layout.hxx) for one implementation, dispatch entry by dispatch
// entry: the read-only (protocol_view<const T>) and read-write
// (protocol_view<T>) counterparts of protocol<T, Allocator>'s own vtable
// population above, using the same template-for + thunk.hxx/conformance.hxx
// pattern.
template <typename Interface, typename Implementation>
consteval const_view_vtable<Interface> build_const_view_vtable() {
  using ConstViewVtable = const_view_vtable<Interface>;
  ConstViewVtable vt{};
  template for (constexpr std::meta::info member : std::define_static_array(
                    interface_member_functions(^^Interface))) {
    if constexpr (std::meta::is_const(member)) {
      constexpr std::meta::info entry =
          find_data_member(^^ConstViewVtable, vtable_slot_name(member));
      constexpr std::meta::info merged_type = candidate_overload_set_type(
          resolve_implementation_candidates(member, ^^Implementation),
          ^^const Implementation&);
      // clang-format off
      using Thunk = erased_call_thunk<Implementation, typename[:merged_type:], typename[:member_function_type(member):], true, std::meta::is_noexcept(member)>;
      // clang-format on
      vt.[:entry:] = &Thunk::call;
    }
  }
  return vt;
}

template <typename Interface, typename Implementation>
inline constexpr const_view_vtable<Interface> const_view_vtable_for =
    build_const_view_vtable<Interface, Implementation>();

template <typename Interface, typename Implementation>
consteval view_vtable<Interface> build_view_vtable() {
  using ViewVtable = view_vtable<Interface>;
  ViewVtable vt{};
  vt.const_view = const_view_vtable_for<Interface, Implementation>;
  template for (constexpr std::meta::info member : std::define_static_array(
                    interface_member_functions(^^Interface))) {
    constexpr std::meta::info entry =
        find_data_member(^^ViewVtable, vtable_slot_name(member));
    constexpr std::meta::info merged_type = candidate_overload_set_type(
        resolve_implementation_candidates(member, ^^Implementation),
        ^^Implementation&);
    // clang-format off
    using Thunk = erased_call_thunk<Implementation, typename[:merged_type:], typename[:member_function_type(member):], false, std::meta::is_noexcept(member)>;
    // clang-format on
    vt.[:entry:] = &Thunk::call;
  }
  return vt;
}

template <typename Interface, typename Implementation>
inline constexpr view_vtable<Interface> view_vtable_for =
    build_view_vtable<Interface, Implementation>();

// Copies every data member `to` has from the same-named member of `from`.
// The shared implementation behind every map_*_vtable_members overload
// below: each vtable shape (owning, const-view, view) differs only in
// which fields it has, not in how narrowing one to another works.
template <typename FromVtable, typename ToVtable>
void copy_matching_by_name(const FromVtable* from, ToVtable* to) {
  template for (constexpr std::meta::info to_member :
                std::define_static_array(std::meta::nonstatic_data_members_of(
                    ^^ToVtable, std::meta::access_context::current()))) {
    constexpr std::string_view name = std::meta::identifier_of(to_member);
    constexpr std::meta::info from_member =
        find_data_member(^^FromVtable, name);
    to->[:to_member:] = from->[:from_member:];
  }
}

// const_view_vtable<Interface> and view_vtable<Interface> (vtable_layout.hxx)
// are local classes, declared inside make_const_view_vtable_info and
// make_view_vtable_info in this namespace.
//
// A function-local class's namespace for ADL is the namespace of the
// function that declares it, not the enclosing namespace. That's different
// from protocol<T,Allocator>::vtable, a member class of protocol, whose
// associated namespace is xyz directly.
//
// protocol.h's get_const_vtable/get_vtable call these two unqualified and
// rely on ADL, so they must live here, in xyz::reflection_detail, to be
// found. map_owning_vtable_members has no such restriction and correctly
// lives in xyz instead. Moving these two into xyz produces "not declared in
// this scope, and no declarations were found by argument-dependent lookup."

// const_view_vtable<Interface> is flat, so narrowing it is exactly
// copy_matching_by_name.
template <typename FromVtable, typename ToVtable>
void map_const_vtable_members(const FromVtable* from, ToVtable* to) {
  copy_matching_by_name(from, to);
}

// view_vtable<Interface> additionally has a const_view sub-object: narrowed
// the same way, recursively, then every other (non-const_view) entry is
// narrowed like the owning vtable.
template <typename FromVtable, typename ToVtable>
void map_vtable_members(const FromVtable* from, ToVtable* to) {
  map_const_vtable_members(&from->const_view, &to->const_view);
  template for (constexpr std::meta::info to_member :
                std::define_static_array(std::meta::nonstatic_data_members_of(
                    ^^ToVtable, std::meta::access_context::current()))) {
    constexpr std::string_view name = std::meta::identifier_of(to_member);
    if constexpr (name != "const_view") {
      constexpr std::meta::info from_member =
          find_data_member(^^FromVtable, name);
      to->[:to_member:] = from->[:from_member:];
    }
  }
}

}  // namespace reflection_detail

// protocol<Interface, Allocator>'s own nested vtable is already the owning
// vtable used for narrowing. This trait just names it the way protocol.h's
// get_owning_vtable expects, matching how the Python/libclang backend's
// generated protocol_owning_vtable_traits<::xyz::A, Allocator> is a bare
// alias to protocol<::xyz::A, Allocator>::vtable, not a separately-designed
// type.
template <typename Interface, typename Allocator>
struct protocol_owning_vtable_traits {
  using vtable = typename protocol<Interface, Allocator>::vtable;
};

// protocol_view<T>/protocol_view<const T>'s narrowing counterpart to
// protocol_owning_vtable_traits above, used by protocol.h's
// get_const_vtable/get_vtable.
template <typename Interface>
struct protocol_vtable_traits {
  using const_vtable = reflection_detail::const_view_vtable<Interface>;
  using vtable = reflection_detail::view_vtable<Interface>;
};

namespace reflection_detail {

// Narrows a view_vtable<FromInterface>* to a view_vtable<ToInterface>*
// (protocol<T,Allocator>'s own view_vt field), using the same cached-mapping
// mechanism get_vtable (protocol.h) uses. It's keyed directly off the
// concrete FromViewVtable/ToViewVtable types deduced from view_vt's own
// pointer type, since a vtable struct here carries no Interface type of its
// own to look protocol_vtable_traits<Interface> up by.
template <typename FromViewVtable, typename ToViewVtable>
const ToViewVtable* narrow_view_vtable_pointer(const FromViewVtable* source) {
  static const char conversion_anchor = 0;
  auto mapping_function = [](const void* from, void* to) {
    map_vtable_members(static_cast<const FromViewVtable*>(from),
                       static_cast<ToViewVtable*>(to));
  };
  return static_cast<const ToViewVtable*>(get_mapped_vtable(
      source, &conversion_anchor, sizeof(ToViewVtable), mapping_function));
}

}  // namespace reflection_detail

// Narrows a source owning vtable to a target one by copying every entry the
// target has, by exact name, from the matching entry in the source. That
// includes xyz_protocol_clone/_move/_destroy, which every protocol's own
// vtable declares.
//
// The exception is view_vt (protocol<T,Allocator>'s own vtable field): it
// points to a different view_vtable<Interface> per Interface, so it needs
// its own recursive narrowing instead of a same-type copy.
//
// FromVtable and ToVtable are deduced directly from the pointer arguments,
// not from an Interface/Allocator template argument list. The Python/
// libclang backend instead generates one hardcoded overload per interface
// pair; this single generic definition works for any (FromInterface,
// ToInterface, Allocator) because it operates on the two concrete vtable
// struct types directly, without needing to know which interfaces they
// came from.
//
// get_owning_vtable (protocol.h) finds this function via ADL: FromVtable
// and ToVtable are nested types of xyz::protocol<...>, so this function
// must live in namespace xyz, not xyz::reflection_detail, to be visible
// there.
template <typename FromVtable, typename ToVtable>
void map_owning_vtable_members(const FromVtable* from, ToVtable* to) {
  template for (constexpr std::meta::info to_member :
                std::define_static_array(std::meta::nonstatic_data_members_of(
                    ^^ToVtable, std::meta::access_context::current()))) {
    constexpr std::string_view name = std::meta::identifier_of(to_member);
    if constexpr (name == "view_vt") {
      to->view_vt = reflection_detail::narrow_view_vtable_pointer<
          std::remove_const_t<std::remove_pointer_t<decltype(from->view_vt)>>,
          std::remove_const_t<std::remove_pointer_t<decltype(to->view_vt)>>>(
          from->view_vt);
    } else {
      constexpr std::meta::info from_member =
          reflection_detail::find_data_member(^^FromVtable, name);
      to->[:to_member:] = from->[:from_member:];
    }
  }
}

template <typename T, typename Allocator>
class protocol : public reflection_detail::protocol_bases<T, Allocator> {
  template <typename, typename, std::meta::info, typename, typename...>
  friend struct reflection_detail::protocol_single_overload_wrapper;
  template <typename, typename>
  friend class protocol;
  friend class protocol_view<T>;
  friend class protocol_view<const T>;

  using clone_or_move_fn = void* (*)(void*, const Allocator&);
  using destroy_fn = void (*)(void*, const Allocator&);
  using view_vtable_ptr = const reflection_detail::view_vtable<T>*;

  // protocol's own vtable: clone/move/destroy, a pointer to the
  // allocator-independent shape vtable protocol_view narrows against
  // (view_vt), plus one entry per interface member, all erased through
  // void* (never const void*). The owning object itself provides both
  // const and non-const access paths, so a single erased pointer kind
  // suffices for its own entries, unlike vtable_layout.hxx's
  // view_vtable/const_view_vtable split that view_vt points to.
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
        std::meta::data_member_spec(^^view_vtable_ptr,
                                    {
                                        .name = "view_vt"}),
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
      vt.view_vt = &reflection_detail::view_vtable_for<T, Implementation>;
      template for (constexpr std::meta::info member : std::define_static_array(
                        reflection_detail::interface_member_functions(^^T))) {
        constexpr std::meta::info entry = reflection_detail::find_data_member(
            ^^vtable, reflection_detail::vtable_slot_name(member));
        constexpr std::meta::info merged_type =
            reflection_detail::candidate_overload_set_type(
                reflection_detail::resolve_implementation_candidates(
                    member, ^^Implementation),
                ^^Implementation&);
        // clang-format off
        using Thunk = reflection_detail::erased_call_thunk<Implementation, typename[:merged_type:], typename[:reflection_detail::member_function_type(member):], false, std::meta::is_noexcept(member)>;
        // clang-format on
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

template <typename T>
class protocol_view<const T>
    : public reflection_detail::protocol_const_view_bases<T> {
  template <typename, std::meta::info, typename, typename...>
  friend struct reflection_detail::protocol_const_view_single_overload_wrapper;
  template <typename>
  friend class protocol_view;

  const void* ptr_;
  const reflection_detail::const_view_vtable<T>* vptr_;

  constexpr protocol_view(
      const void* ptr,
      const reflection_detail::const_view_vtable<T>* vptr) noexcept
      : ptr_(ptr), vptr_(vptr) {}

  template <typename Alloc>
  static const void* checked_ptr(const protocol<T, Alloc>& p) noexcept {
    assert(!p.valueless_after_move());
    return p.p_;
  }

 public:
  template <typename U>
    requires reflection_detail::reflection_protocol_const_concept<U, T> &&
                 not_protocol_or_view<U>
  constexpr protocol_view(const U& obj) noexcept
      : ptr_(std::addressof(obj)),
        vptr_(
            &reflection_detail::const_view_vtable_for<T,
                                                      std::remove_cvref_t<U>>) {
  }

  template <typename U>
    requires reflection_detail::reflection_protocol_const_concept<U, T> &&
                 not_protocol_or_view<U>
  protocol_view(const U&&) = delete;

  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>& p) noexcept
      : ptr_(checked_ptr(p)), vptr_(&p.vtable_->view_vt->const_view) {}

  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>&&) = delete;

  template <typename Alloc>
  protocol_view(protocol<T, Alloc>& p) noexcept
      : ptr_(checked_ptr(p)), vptr_(&p.vtable_->view_vt->const_view) {}

  template <typename Alloc>
  protocol_view(protocol<T, Alloc>&&) = delete;

  constexpr protocol_view(protocol_view<T> other) noexcept;

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol_view<const Other>& other) noexcept
      : ptr_(other.ptr_), vptr_(get_const_vtable<Other, T>(other.vptr_)) {}

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol_view<Other>& other) noexcept
      : ptr_(other.ptr_),
        vptr_(get_const_vtable<Other, T>(&other.vptr_->const_view)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol<Other, Alloc>& p) noexcept
      : protocol_view(protocol_view<const Other>(p)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol<Other, Alloc>&&) = delete;

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(protocol<Other, Alloc>& p) noexcept
      : protocol_view(protocol_view<Other>(p)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(protocol<Other, Alloc>&&) = delete;
};

template <typename T>
class protocol_view : public reflection_detail::protocol_view_bases<T> {
  template <typename, std::meta::info, typename, typename...>
  friend struct reflection_detail::protocol_view_single_overload_wrapper;
  template <typename>
  friend class protocol_view;

  void* ptr_;
  const reflection_detail::view_vtable<T>* vptr_;

  template <typename Alloc>
  static void* checked_ptr(protocol<T, Alloc>& p) noexcept {
    assert(!p.valueless_after_move());
    return p.p_;
  }

 public:
  template <typename U>
    requires reflection_detail::reflection_protocol_concept<U, T> &&
                 not_protocol_or_view<U>
  constexpr protocol_view(U& obj) noexcept
      : ptr_(std::addressof(obj)),
        vptr_(&reflection_detail::view_vtable_for<T, std::remove_cvref_t<U>>) {}

  template <typename U>
    requires reflection_detail::reflection_protocol_concept<U, T> &&
                 not_protocol_or_view<U>
  protocol_view(const U&&) = delete;

  template <typename Alloc>
  protocol_view(protocol<T, Alloc>& p) noexcept
      : ptr_(checked_ptr(p)), vptr_(p.vtable_->view_vt) {}

  template <typename Alloc>
  protocol_view(protocol<T, Alloc>&&) = delete;

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol_view<Other>& other) noexcept
      : ptr_(other.ptr_), vptr_(get_vtable<Other, T>(other.vptr_)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(protocol<Other, Alloc>& p) noexcept
      : protocol_view(protocol_view<Other>(p)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(protocol<Other, Alloc>&&) = delete;
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

template <typename Interface, std::meta::info Member, typename R,
          typename... Ps>
R protocol_view_single_overload_wrapper<Interface, Member, R,
                                        Ps...>::operator()(Ps... ps) const
    noexcept(std::meta::is_noexcept(Member)) {
  using Owner = protocol_view<Interface>;
  using Combined = protocol_view_member_wrapper<Interface, Member>;
  constexpr std::meta::info representative =
      members_named_like(^^Interface, Member)[0];
  using Base = forwarder_base<Combined, representative>;
  using ViewVtable = view_vtable<Interface>;
  const auto* combined = static_cast<const Combined*>(this);
  const auto* base =
      static_cast<const Base*>(static_cast<const void*>(combined));
  const auto* owner = static_cast<const Owner*>(base);
  constexpr std::meta::info entry =
      find_data_member(^^ViewVtable, vtable_slot_name(Member));
  return owner->vptr_->[:entry:](owner->ptr_, std::forward<Ps>(ps)...);
}

template <typename Interface, std::meta::info Member, typename R,
          typename... Ps>
R protocol_const_view_single_overload_wrapper<Interface, Member, R,
                                              Ps...>::operator()(Ps... ps) const
    noexcept(std::meta::is_noexcept(Member)) {
  using Owner = protocol_view<const Interface>;
  using Combined = protocol_const_view_member_wrapper<Interface, Member>;
  // Unlike the owning/mutable-view wrappers, the representative here must
  // be the first *const* sibling: members_named_like(...)[0] could be a
  // non-const overload the const view never exposes at all.
  constexpr std::meta::info representative =
      first_const_member_named_like(^^Interface, Member);
  using Base = forwarder_base<Combined, representative>;
  using ConstViewVtable = const_view_vtable<Interface>;
  const auto* combined = static_cast<const Combined*>(this);
  const auto* base =
      static_cast<const Base*>(static_cast<const void*>(combined));
  const auto* owner = static_cast<const Owner*>(base);
  constexpr std::meta::info entry =
      find_data_member(^^ConstViewVtable, vtable_slot_name(Member));
  return owner->vptr_->[:entry:](owner->ptr_, std::forward<Ps>(ps)...);
}

}  // namespace reflection_detail

template <typename T>
inline constexpr protocol_view<const T>::protocol_view(
    protocol_view<T> other) noexcept
    : ptr_(other.ptr_), vptr_(&other.vptr_->const_view) {}

}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_HXX_

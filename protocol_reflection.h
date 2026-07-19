/* Copyright (c) 2026 The XYZ Protocol Authors. All Rights Reserved.

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
// C++26-reflection code-generation backend for xyz::protocol /
// xyz::protocol_view. Instead of generating a per-interface header at build
// time, this header generates the same machinery inside the compiler using
// P2996 reflection. Any plain struct, class, or template instantiation works
// as an interface type automatically; no macro, build step, or per-type
// opt-in annotation is required.
//
// Requires GCC 16+ with `-std=c++26 -freflection`; fails to compile
// otherwise, rather than silently no-op'ing. Defines xyz::protocol /
// xyz::protocol_view as primary templates (protocol.h's own placeholder
// primary templates must therefore not also be defined when this header
// is included, or the two would conflict; see protocol.h for how it
// arranges that).
//
// Conformance to an interface is checked by a concept
// (reflection_protocol_concept / reflection_protocol_const_concept),
// exactly as strictly as any C++ concept: a stored type either satisfies
// it or it doesn't, and protocol<T>'s constructors are constrained on
// exactly that. The concept is implemented via real invocability against
// the stored type's own overload set -- every candidate with the
// interface member's name is merged into one callable type and the
// compiler decides what's callable -- rather than this backend
// hand-comparing parameter types. This mirrors the Python backend's
// `requires`-expression-based concept (itself a real call, e.g.
// `t.method(std::declval<Arg>()...)`) and Ryan Keane's rjk::duck technique
// (https://ryanjk5.github.io/posts/rjk-duck/).
//
// Documented, deliberate limitations of this backend:
// - Interface members must be implemented as direct members of the stored
//   type: member functions inherited from a base class of the implementation
//   are not found by the reflection-based member resolution.
// - An interface member function named `swap` or `valueless_after_move`
//   would be hidden by protocol's own member of that name (undetectable via
//   the forwarders, which are inherited), so it is instead rejected with a
//   static_assert naming the interface.
// - Generated member functions are not marked constexpr (out of scope for
//   this backend for now).
#ifndef XYZ_PROTOCOL_REFLECTION_H_
#define XYZ_PROTOCOL_REFLECTION_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <cassert>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "protocol.h"
#include "protocol_reflection_detail/conformance.h"
#include "protocol_reflection_detail/members.h"
#include "protocol_reflection_detail/naming.h"
#include "protocol_reflection_detail/operator_forwarders.h"
#include "protocol_reflection_detail/thunk.h"
#include "protocol_reflection_detail/types.h"
#include "protocol_reflection_detail/vtable_layout.h"

namespace xyz {

namespace reflection_detail {

using std::meta::info;

// ---------------------------------------------------------------------------
// Vtable instances per (interface, implementation) pair
// ---------------------------------------------------------------------------

// Allocate storage for one Implementation via a rebound copy of `allocator`
// and construct it from `arguments`, deallocating if construction throws.
// Shared by the vtable lifetime operations and protocol's constructors.
template <typename Implementation, typename Allocator, typename... Arguments>
void* allocate_and_construct(const Allocator& allocator,
                             Arguments&&... arguments) {
  using implementation_allocator = typename std::allocator_traits<
      Allocator>::template rebind_alloc<Implementation>;
  using implementation_allocator_traits =
      std::allocator_traits<implementation_allocator>;
  implementation_allocator rebound_allocator(allocator);
  auto memory = implementation_allocator_traits::allocate(rebound_allocator, 1);
  try {
    implementation_allocator_traits::construct(
        rebound_allocator, memory, std::forward<Arguments>(arguments)...);
    return memory;
  } catch (...) {
    implementation_allocator_traits::deallocate(rebound_allocator, memory, 1);
    throw;
  }
}

// The allocator-aware lifetime operations, forming the fixed part of
// vtable_impl.
template <typename Implementation, typename Allocator>
struct allocator_lifetime {
  using implementation_allocator = typename std::allocator_traits<
      Allocator>::template rebind_alloc<Implementation>;
  using implementation_allocator_traits =
      std::allocator_traits<implementation_allocator>;

  static void* clone(void* erased, const Allocator& allocator) {
    return allocate_and_construct<Implementation>(
        allocator, *static_cast<Implementation*>(erased));
  }

  static void* move_construct(void* erased, const Allocator& allocator) {
    return allocate_and_construct<Implementation>(
        allocator, std::move(*static_cast<Implementation*>(erased)));
  }

  static void destroy(void* erased, const Allocator& allocator) {
    auto* self = static_cast<Implementation*>(erased);
    implementation_allocator rebound_allocator(allocator);
    implementation_allocator_traits::destroy(rebound_allocator, self);
    implementation_allocator_traits::deallocate(rebound_allocator, self, 1);
  }
};

// One vtable entry value: the address of the exactly-typed thunk for the
// Index-th interface member in the given selection.
template <typename Interface, typename Implementation, std::size_t Index>
consteval auto make_vtable_entry() {
  constexpr info member = interface_members<Interface>[Index];
  constexpr bool ConstErased = std::meta::is_const(member);
  constexpr info merged =
      make_candidate_overload_set(^^Implementation, member, ConstErased);
  constexpr info erased_pointer_type = ConstErased ? ^^const void* : ^^void*;
  using ThunkPointer = [:vtable_entry_pointer_type(member,
                                                   erased_pointer_type):];
  if constexpr (merged == info{}) {
    return ThunkPointer(nullptr);
  } else {
    using Signature = [:member_function_type(member):];
    return ThunkPointer(
        &erased_call_thunk<Implementation, merged, Signature, ConstErased,
                           std::meta::is_noexcept(member)>::call);
  }
}

template <typename Interface, typename Implementation, std::size_t... Indexes>
consteval auto make_view_entries(std::index_sequence<Indexes...>) {
  return typename view_vtable<Interface>::view_entries{
      make_vtable_entry<Interface, Implementation, Indexes>()...};
}

template <typename Interface, typename Allocator, typename Implementation,
          std::size_t... Indexes>
consteval auto make_owning_entries(std::index_sequence<Indexes...>) {
  return typename owning_vtable<Interface, Allocator>::owning_entries{
      make_vtable_entry<Interface, Implementation, Indexes>()...};
}

template <typename Interface, typename Implementation>
inline constexpr typename view_vtable<Interface>::vtable view_vtable_for = {
    make_view_entries<Interface, Implementation>(
        std::make_index_sequence<interface_members<Interface>.size()>())};

template <typename Interface, typename Allocator, typename Implementation>
inline constexpr
    typename owning_vtable<Interface, Allocator>::vtable owning_vtable_for = {
        &allocator_lifetime<Implementation, Allocator>::clone,
        &allocator_lifetime<Implementation, Allocator>::move_construct,
        &allocator_lifetime<Implementation, Allocator>::destroy,
        &view_vtable_for<Interface, Implementation>,
        make_owning_entries<Interface, Allocator, Implementation>(
            std::make_index_sequence<interface_members<Interface>.size()>())};

// ---------------------------------------------------------------------------
// Vtable narrowing maps, found by ADL from protocol.h's get_const_vtable /
// get_vtable / get_owning_vtable. Entries are copied by name: every entry of
// the target vtable must exist, identically named (same signature hash), in
// the source vtable — which is exactly the subset relationship narrowing
// conversions rely on.
// ---------------------------------------------------------------------------

template <typename FromEntries, typename ToEntries>
void copy_vtable_entries(const FromEntries& from, ToEntries& to) {
  static constexpr auto to_members =
      std::define_static_array(std::meta::nonstatic_data_members_of(
          ^^ToEntries, std::meta::access_context::current()));
  template for (constexpr info to_member : to_members) {
    constexpr info from_member =
        data_member_named(^^FromEntries, std::meta::identifier_of(to_member));
    static_assert(from_member != info{},
                  "reflection backend: narrowing conversion requires every "
                  "target interface member to exist in the source interface "
                  "with an identical signature");
    to.[:to_member:] = from.[:from_member:];
  }
}

template <typename Vtable>
concept reflection_view_vtable =
    requires { typename Vtable::xyz_reflection_view_vtable_tag; };

template <typename Vtable>
concept reflection_owning_vtable =
    requires { typename Vtable::xyz_reflection_owning_vtable_tag; };

template <reflection_view_vtable FromVtable, reflection_view_vtable ToVtable>
void map_const_vtable_members(const FromVtable* from, ToVtable* to) {
  copy_vtable_entries(from->entries, to->entries);
}

template <reflection_view_vtable FromVtable, reflection_view_vtable ToVtable>
void map_vtable_members(const FromVtable* from, ToVtable* to) {
  copy_vtable_entries(from->entries, to->entries);
}

template <reflection_owning_vtable FromVtable,
          reflection_owning_vtable ToVtable>
void map_owning_vtable_members(const FromVtable* from, ToVtable* to) {
  to->xyz_protocol_clone = from->xyz_protocol_clone;
  to->xyz_protocol_move = from->xyz_protocol_move;
  to->xyz_protocol_destroy = from->xyz_protocol_destroy;
  to->view_vt = get_vtable<typename FromVtable::protocol_type,
                           typename ToVtable::protocol_type>(from->view_vt);
  copy_vtable_entries(from->entries, to->entries);
}

// ---------------------------------------------------------------------------
// Forwarding call wrappers
//
// GCC 16 cannot splice a reflection as the declarator-id of a new member
// function, so per-method forwarders are synthesized as data members (named
// by the interface member's identifier) whose type overloads operator() with
// the exact interface signature. Overloaded interface names become one data
// member whose type merges one wrapper per overload.
//
// Recovering the owning protocol / protocol_view object from inside that
// wrapper's operator() is two ordinary, always-well-defined casts, not one
// address-equality assumption. named_forwarders<Interface, Owner, ConstOnly,
// ForceConstCall>::single_member_wrapper<GroupKeyMember> holds exactly one
// data member: that group's (possibly overload-merged) forwarding_call. A
// standard-layout class with exactly one non-static data member shares that
// member's address ([class.mem]), so casting from the member back to its
// enclosing single_member_wrapper via void* is always well-defined, with no
// [[no_unique_address]] or layout hoping involved. From there, casting up
// from single_member_wrapper to Owner is an ordinary base-to-derived
// static_cast: Owner (transitively, through named_forwarders<...>::type =
// forwarder_group<single_member_wrapper<Key1>, ...>) really does derive from
// every single_member_wrapper, so the compiler applies whatever pointer
// adjustment that base's actual offset needs -- offset zero is never
// required or assumed for this step. This mirrors Duck's trace_to_duck
// (https://github.com/RyanJK5/rjk-duck), whose vtable_function recovers its
// owning duck the same two-step way, through its own vtable_function_wrapper
// (Duck's equivalent of single_member_wrapper, hand-written there rather
// than reflected, since Duck doesn't need one generated per interface member
// of an arbitrary type at compile time).
//
// define_aggregate can only run from inside a `consteval { ... }` block, and
// that block cannot have another class's scope intervening between it and
// the incomplete type it completes -- so single_member_wrapper is nested
// inside named_forwarders itself (one uniquely-named member, one nested
// class, completed by the consteval block right below it, sharing its
// scope), rather than living at namespace scope alongside forwarder_group.
// ---------------------------------------------------------------------------

template <typename NamedForwarders, info Member, info GroupKeyMember,
          typename Signature, bool ConstCall, bool IsNoexcept>
struct forwarding_call;

template <typename NamedForwarders, info Member, info GroupKeyMember,
          typename ReturnType, typename... ParameterTypes, bool IsNoexcept>
struct forwarding_call<NamedForwarders, Member, GroupKeyMember,
                       ReturnType(ParameterTypes...), false, IsNoexcept> {
  ReturnType operator()(ParameterTypes... parameters) noexcept(IsNoexcept) {
    void* voided = this;
    auto* wrapper =
        static_cast<typename NamedForwarders::template single_member_wrapper<
            GroupKeyMember>*>(voided);
    auto* owner = static_cast<typename NamedForwarders::owner_type*>(wrapper);
    return owner->template dispatch_reflected_member<Member>(
        std::forward<ParameterTypes>(parameters)...);
  }
};

template <typename NamedForwarders, info Member, info GroupKeyMember,
          typename ReturnType, typename... ParameterTypes, bool IsNoexcept>
struct forwarding_call<NamedForwarders, Member, GroupKeyMember,
                       ReturnType(ParameterTypes...), true, IsNoexcept> {
  ReturnType operator()(ParameterTypes... parameters) const
      noexcept(IsNoexcept) {
    const void* voided = this;
    const auto* wrapper =
        static_cast<const typename NamedForwarders::
                        template single_member_wrapper<GroupKeyMember>*>(
            voided);
    const auto* owner =
        static_cast<const typename NamedForwarders::owner_type*>(wrapper);
    return owner->template dispatch_reflected_member<Member>(
        std::forward<ParameterTypes>(parameters)...);
  }
};

template <typename... Overloads>
struct overloaded_calls : Overloads... {
  using Overloads::operator()...;
};

// Interface members grouped for forwarder generation, preserving declaration
// order within and across groups: named members group by identifier,
// operator members by operator kind. Each call selects one population via
// group_operators — the two populations need different downstream mechanisms
// (a data member cannot be named `operator+`), but share this grouping.
consteval std::vector<std::vector<info>> grouped_interface_members(
    const std::vector<info>& members, bool group_operators) {
  std::vector<std::vector<info>> groups;
  std::vector<bool> grouped(members.size(), false);
  for (std::size_t index = 0; index < members.size(); ++index) {
    if (grouped[index]) continue;
    if (std::meta::has_identifier(members[index]) == group_operators) continue;
    std::vector<info> group;
    for (std::size_t other = index; other < members.size(); ++other) {
      if (grouped[other]) continue;
      if (std::meta::has_identifier(members[other]) == group_operators) {
        continue;
      }
      bool same_group = group_operators
                            ? std::meta::operator_of(members[other]) ==
                                  std::meta::operator_of(members[index])
                            : std::meta::identifier_of(members[other]) ==
                                  std::meta::identifier_of(members[index]);
      if (!same_group) continue;
      grouped[other] = true;
      group.push_back(members[other]);
    }
    groups.push_back(group);
  }
  return groups;
}

// Combines one single_member_wrapper per uniquely-named interface member
// through ordinary multiple inheritance.
template <typename... Wrappers>
struct forwarder_group : public Wrappers... {};

// One base per uniquely-named interface member (operators are handled
// separately, since a data member cannot be named `operator+`), combined
// via forwarder_group. ForceConstCall makes every wrapper's operator() const
// regardless of the interface member's constness, matching the generated
// protocol_view classes whose forwarders are all const.
template <typename Interface, typename Owner, bool ConstOnly,
          bool ForceConstCall>
struct named_forwarders {
  using owner_type = Owner;

  // GroupKeyMember is a group's first interface member: its own info both
  // identifies the group and, via identifier_of, names the data member
  // below. Declared here, completed by the consteval block that follows,
  // sharing this scope as define_aggregate requires.
  template <info GroupKeyMember>
  struct single_member_wrapper;

  consteval static info wrapper_type_for(const std::vector<info>& overloads) {
    std::vector<info> overload_wrappers;
    for (info member : overloads) {
      overload_wrappers.push_back(std::meta::substitute(
          ^^forwarding_call,
          {
              ^^named_forwarders, std::meta::reflect_constant(member),
              std::meta::reflect_constant(overloads.front()),
              member_function_type(member),
              std::meta::reflect_constant(ForceConstCall ||
                                          std::meta::is_const(member)),
              std::meta::reflect_constant(std::meta::is_noexcept(member))}));
    }
    return overload_wrappers.size() == 1
               ? overload_wrappers.front()
               : std::meta::substitute(^^overloaded_calls, overload_wrappers);
  }

  consteval {
    for (const std::vector<info>& overloads : grouped_interface_members(
             interface_member_functions(^^Interface, ConstOnly), false)) {
      std::meta::define_aggregate(
          std::meta::substitute(
              ^^single_member_wrapper,
              {
                  std::meta::reflect_constant(overloads.front())}),
          {std::meta::data_member_spec(
              wrapper_type_for(overloads),
              {.name = std::meta::identifier_of(overloads.front()),
               .no_unique_address = true})});
    }
  }

  consteval static std::vector<info> bases() {
    std::vector<info> result;
    for (const std::vector<info>& overloads : grouped_interface_members(
             interface_member_functions(^^Interface, ConstOnly), false)) {
      result.push_back(std::meta::substitute(
          ^^single_member_wrapper,
          {
              std::meta::reflect_constant(overloads.front())}));
    }
    return result;
  }

  using type = typename[:std::meta::substitute(^^forwarder_group, bases()):];
};

// ---------------------------------------------------------------------------
// Operator forwarding
//
// A data member cannot be named `operator+`, so interface operators cannot
// use the named-forwarder mechanism above. Instead, operator_forwarder and
// operator_join (protocol_reflection_detail/operator_forwarders.h) are
// generated once per operator kind offline, rather than reimplemented here:
// forwarding an operator needs a real `operator<symbol>` declaration, with
// the symbol itself a literal token in source, the same limitation
// https://github.com/RyanJK5/rjk-duck works around with its own
// generate_operators.py. The per-interface set of operator forwarders is
// combined into an empty base class of protocol / protocol_view below.
//
// operator= needs one extra thing the other operators don't: protocol and
// protocol_view each need their own operator= (hand-written for value
// semantics, or left to the compiler to generate), and in C++ a class's own
// operator=, even a compiler-generated one, hides any operator= it would
// otherwise inherit from a base class. So every class in this merge chain
// that doesn't declare a member of its own needs an explicit
// using-declaration bringing the inherited operator= back into scope:
// combined_operator_joins below, and each of protocol, protocol_view<const
// T>, and protocol_view<T> further down this file.
// ---------------------------------------------------------------------------

// Every level of this merge hierarchy that doesn't declare its own members
// gets an implicitly-declared operator= of its own (ordinary C++), which
// would otherwise hide any operator= a Join brings in from its Forwarders --
// hence the using-declaration below, mirroring the one operator_join itself
// already needs for the same reason.
template <typename... Joins>
struct combined_operator_joins : Joins... {
  using Joins::operator=...;
};

consteval info make_operator_forwarders(info interface_type, info owner_type,
                                        bool const_only,
                                        bool force_const_call) {
  std::vector<info> joins;
  for (const std::vector<info>& overloads : grouped_interface_members(
           interface_member_functions(interface_type, const_only), true)) {
    std::meta::operators kind = std::meta::operator_of(overloads.front());
    std::vector<info> join_arguments;
    join_arguments.push_back(std::meta::reflect_constant(kind));
    for (info member : overloads) {
      join_arguments.push_back(std::meta::substitute(
          ^^operator_forwarder,
          {
              owner_type, std::meta::reflect_constant(member),
              member_function_type(member), std::meta::reflect_constant(kind),
              std::meta::reflect_constant(force_const_call ||
                                          std::meta::is_const(member)),
              std::meta::reflect_constant(std::meta::is_noexcept(member))}));
    }
    joins.push_back(std::meta::substitute(^^operator_join, join_arguments));
  }
  return std::meta::substitute(^^combined_operator_joins, joins);
}

template <typename Interface, typename Owner, bool ConstOnly,
          bool ForceConstCall>
struct operator_forwarders {
  using type = typename[:make_operator_forwarders(^^Interface, ^^Owner,
                                                  ConstOnly, ForceConstCall):];
};

// Every forwarder base subobject, every forwarder data member inside those
// bases (the named forwarding wrappers), and their bases recursively are
// expected to collapse to offset zero of the owning object: forwarding_call
// and operator_forwarder never hold state, so every wrapper is empty and
// [[no_unique_address]] should let them all overlap. Neither the two-step
// cast in forwarding_call nor the direct static_cast<Owner*>(this) in
// operator_forwarder depends on that collapse for correctness: both apply
// whatever adjustment the real offset needs. But [[no_unique_address]] is
// only a request, not a guarantee, so this asserts the collapse happened
// rather than silently paying for padding an implementation is free to
// insert if it ever doesn't.
consteval bool forwarders_at_offset_zero(info class_type) {
  for (info base :
       std::meta::bases_of(class_type, std::meta::access_context::current())) {
    if (std::meta::offset_of(base).bytes != 0) return false;
    info base_type = std::meta::dealias(std::meta::type_of(base));
    for (info data_member : std::meta::nonstatic_data_members_of(
             base_type, std::meta::access_context::current())) {
      if (std::meta::offset_of(data_member).bytes != 0) return false;
    }
    if (!forwarders_at_offset_zero(base_type)) return false;
  }
  return true;
}

}  // namespace reflection_detail

// ---------------------------------------------------------------------------
// Registry hookup: matches the shape expected by get_const_vtable /
// get_vtable / get_owning_vtable in protocol.h, so they work unmodified
// regardless of backend.
// ---------------------------------------------------------------------------

template <typename T>
struct protocol_vtable_traits {
  using const_vtable = typename reflection_detail::view_vtable<T>::vtable;
  using vtable = typename reflection_detail::view_vtable<T>::vtable;
};

template <typename T, typename Allocator>
struct protocol_owning_vtable_traits {
  using vtable =
      typename reflection_detail::owning_vtable<T, Allocator>::vtable;
};

// ---------------------------------------------------------------------------
// protocol<T, Allocator> — primary template definition. The constructors,
// assignment, swap, and destructor are hand-written here (define_aggregate
// can only produce data members, so this part isn't reflection-generated);
// the per-interface concepts and vtable types they use are the reflection
// equivalents built above. Named-method forwarding comes from the
// named_forwarders empty base.
// ---------------------------------------------------------------------------

template <typename T, typename Allocator>
class protocol
    : public reflection_detail::named_forwarders<T, protocol<T, Allocator>,
                                                 false, false>::type,
      public reflection_detail::operator_forwarders<T, protocol<T, Allocator>,
                                                    false, false>::type {
  friend class protocol_view<T>;
  friend class protocol_view<const T>;
  template <typename, typename>
  friend class protocol;
  template <typename, typename>
  friend struct protocol_owning_vtable_traits;
  template <typename, std::meta::info, std::meta::info, typename, bool, bool>
  friend struct reflection_detail::forwarding_call;
  template <typename, std::meta::info, typename, std::meta::operators, bool,
            bool>
  friend struct reflection_detail::operator_forwarder;

  static_assert(!reflection_detail::has_reserved_interface_member_name(^^T),
                "xyz::protocol: interface must not declare a member function "
                "named 'swap' or 'valueless_after_move' - these names are "
                "reserved for protocol's own public members and would be "
                "silently hidden by ordinary C++ name-hiding rules");
  static_assert(
      !reflection_detail::has_rvalue_qualified_interface_member(^^T),
      "xyz::protocol: interface must not declare an rvalue-qualified (&&) "
      "member function - this backend does not support them");

  using vtable =
      typename reflection_detail::owning_vtable<T, Allocator>::vtable;

  template <std::meta::info Member, typename... Arguments>
  decltype(auto) dispatch_reflected_member(Arguments&&... arguments) {
    static_assert(reflection_detail::forwarders_at_offset_zero(^^protocol));
    constexpr std::meta::info entry = reflection_detail::data_member_named(
        ^^typename reflection_detail::owning_vtable<T,
                                                    Allocator>::owning_entries,
        reflection_detail::vtable_entry_name(Member));
    return vtable_->entries.[:entry:](p_,
                                      std::forward<Arguments>(arguments)...);
  }

  template <std::meta::info Member, typename... Arguments>
  decltype(auto) dispatch_reflected_member(Arguments&&... arguments) const {
    static_assert(reflection_detail::forwarders_at_offset_zero(^^protocol));
    constexpr std::meta::info entry = reflection_detail::data_member_named(
        ^^typename reflection_detail::owning_vtable<T,
                                                    Allocator>::owning_entries,
        reflection_detail::vtable_entry_name(Member));
    return vtable_->entries.[:entry:](p_,
                                      std::forward<Arguments>(arguments)...);
  }

  using allocator_traits = std::allocator_traits<Allocator>;

  template <class U, class... Ts>
  [[nodiscard]] void* create_storage(Ts&&... ts) const {
    return reflection_detail::allocate_and_construct<U>(
        alloc_, std::forward<Ts>(ts)...);
  }

  void* p_;
  const vtable* vtable_;
  [[no_unique_address]] Allocator alloc_;

 public:
  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol(protocol<Other, Allocator>&& other) noexcept(
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

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol(const protocol<Other, Allocator>& other)
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
  protocol(std::allocator_arg_t, const Allocator& alloc,
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
  protocol(std::allocator_arg_t, const Allocator& alloc,
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

  explicit protocol()
    requires std::default_initializable<T> &&
             reflection_protocol_concept<T, T> && std::copy_constructible<T>
      : protocol(std::allocator_arg_t{}, Allocator{}) {}

  template <class U>
  explicit protocol(U&& u)
    requires(!std::same_as<protocol, std::remove_cvref_t<U>>) &&
            not_protocol_or_view<U> &&
            std::copy_constructible<std::remove_cvref_t<U>> &&
            reflection_protocol_concept<U, T>
      : protocol(std::allocator_arg_t{}, Allocator{}, std::forward<U>(u)) {}

  template <class U, class... Ts>
  explicit protocol(std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> && std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> &&
             std::default_initializable<Allocator> &&
             reflection_protocol_concept<U, T>
      : protocol(std::allocator_arg_t{}, Allocator{}, std::in_place_type<U>,
                 std::forward<Ts>(ts)...) {}

  template <class U, class I, class... Ts>
  explicit protocol(std::in_place_type_t<U>, std::initializer_list<I> ilist,
                    Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> &&
             std::constructible_from<U, std::initializer_list<I>, Ts&&...> &&
             std::copy_constructible<U> &&
             std::default_initializable<Allocator> &&
             reflection_protocol_concept<U, T>
      : protocol(std::allocator_arg_t{}, Allocator{}, std::in_place_type<U>,
                 ilist, std::forward<Ts>(ts)...) {}

  protocol(const protocol& other)
      : protocol(std::allocator_arg_t{},
                 allocator_traits::select_on_container_copy_construction(
                     other.alloc_),
                 other) {}

  protocol(protocol&& other) noexcept(allocator_traits::is_always_equal::value)
      : protocol(std::allocator_arg_t{}, other.alloc_, std::move(other)) {}

  explicit protocol(std::allocator_arg_t, const Allocator& alloc)
    requires std::default_initializable<T> && std::copy_constructible<T>
      : alloc_(alloc) {
    p_ = create_storage<T>();
    vtable_ = &reflection_detail::owning_vtable_for<T, Allocator, T>;
  }

  template <class U>
  explicit protocol(std::allocator_arg_t, const Allocator& alloc, U&& u)
    requires(!std::same_as<protocol, std::remove_cvref_t<U>>) &&
            not_protocol_or_view<U> &&
            std::copy_constructible<std::remove_cvref_t<U>> &&
            reflection_protocol_concept<U, T>
      : alloc_(alloc) {
    p_ = create_storage<std::remove_cvref_t<U>>(std::forward<U>(u));
    vtable_ = &reflection_detail::owning_vtable_for<T, Allocator,
                                                    std::remove_cvref_t<U>>;
  }

  template <class U, class... Ts>
  explicit protocol(std::allocator_arg_t, const Allocator& alloc,
                    std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> && std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> && reflection_protocol_concept<U, T>
      : alloc_(alloc) {
    p_ = create_storage<U>(std::forward<Ts>(ts)...);
    vtable_ = &reflection_detail::owning_vtable_for<T, Allocator, U>;
  }

  template <class U, class I, class... Ts>
  explicit protocol(std::allocator_arg_t, const Allocator& alloc,
                    std::in_place_type_t<U>, std::initializer_list<I> ilist,
                    Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> &&
             std::constructible_from<U, std::initializer_list<I>, Ts&&...> &&
             std::copy_constructible<U> && reflection_protocol_concept<U, T>
      : alloc_(alloc) {
    p_ = create_storage<U>(ilist, std::forward<Ts>(ts)...);
    vtable_ = &reflection_detail::owning_vtable_for<T, Allocator, U>;
  }

  protocol(std::allocator_arg_t, const Allocator& alloc, const protocol& other)
      : alloc_(alloc) {
    if (!other.valueless_after_move()) {
      p_ = other.vtable_->xyz_protocol_clone(other.p_, alloc_);
      vtable_ = other.vtable_;
    } else {
      p_ = nullptr;
      vtable_ = nullptr;
    }
  }

  protocol(std::allocator_arg_t, const Allocator& alloc,
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
        } else {
          p_ = nullptr;
          vtable_ = nullptr;
        }
      }
    }
  }

  bool valueless_after_move() const noexcept { return p_ == nullptr; }

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

  // Interface-declared operator= overloads are merged by operator_forwarders
  // the same way every other operator is; the copy-assignment operator above
  // would otherwise hide them entirely (ordinary C++ name hiding, not
  // anything specific to assignment: a derived class's own operator=, even
  // an implicitly-declared one, hides any operator= it would otherwise
  // inherit, unless brought back into scope by a using-declaration).
  using reflection_detail::operator_forwarders<T, protocol<T, Allocator>, false,
                                               false>::type::operator=;

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

// ---------------------------------------------------------------------------
// protocol_view<const T> — the const view.
// ---------------------------------------------------------------------------

template <typename T>
class protocol_view<const T>
    : public reflection_detail::named_forwarders<T, protocol_view<const T>,
                                                 true, true>::type,
      public reflection_detail::operator_forwarders<T, protocol_view<const T>,
                                                    true, true>::type {
  template <typename>
  friend class protocol_view;
  template <typename, std::meta::info, std::meta::info, typename, bool, bool>
  friend struct reflection_detail::forwarding_call;
  template <typename, std::meta::info, typename, std::meta::operators, bool,
            bool>
  friend struct reflection_detail::operator_forwarder;

  using const_vtable = typename reflection_detail::view_vtable<T>::vtable;

  template <std::meta::info Member, typename... Arguments>
  decltype(auto) dispatch_reflected_member(Arguments&&... arguments) const {
    static_assert(
        reflection_detail::forwarders_at_offset_zero(^^protocol_view));
    constexpr std::meta::info entry = reflection_detail::data_member_named(
        ^^typename reflection_detail::view_vtable<T>::view_entries,
        reflection_detail::vtable_entry_name(Member));
    return vptr_->entries.[:entry:](ptr_,
                                    std::forward<Arguments>(arguments)...);
  }

  const void* ptr_;
  const const_vtable* vptr_;

  protocol_view(const void* ptr, const const_vtable* vptr) noexcept
      : ptr_(ptr), vptr_(vptr) {}

  template <typename Alloc>
  static const void* checked_ptr(const protocol<T, Alloc>& p) noexcept {
    assert(!p.valueless_after_move());
    return p.p_;
  }

 public:
  template <typename U>
    requires reflection_protocol_const_concept<U, T> && not_protocol_or_view<U>
  protocol_view(const U& obj) noexcept
      : ptr_(std::addressof(obj)),
        vptr_(&reflection_detail::view_vtable_for<T, std::remove_cvref_t<U>>) {}

  template <typename U>
    requires reflection_protocol_const_concept<U, T> && not_protocol_or_view<U>
  protocol_view(const U&&) = delete;

  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>& p) noexcept
      : ptr_(checked_ptr(p)), vptr_(p.vtable_->view_vt) {}

  template <typename Alloc>
  protocol_view(const protocol<T, Alloc>&&) = delete;

  template <typename Alloc>
  protocol_view(protocol<T, Alloc>& p) noexcept
      : ptr_(checked_ptr(p)), vptr_(p.vtable_->view_vt) {}

  template <typename Alloc>
  protocol_view(protocol<T, Alloc>&&) = delete;

  protocol_view(protocol_view<T> other) noexcept;

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol_view<const Other>& other) noexcept
      : ptr_(other.ptr_), vptr_(get_const_vtable<Other, T>(other.vptr_)) {}

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol_view<Other>& other) noexcept
      : ptr_(other.ptr_), vptr_(get_const_vtable<Other, T>(other.vptr_)) {}

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

  // See protocol's own operator= for why this using-declaration is needed:
  // without it, the implicitly-declared copy-assignment operator this class
  // relies on would hide any operator= inherited from operator_forwarders.
  using reflection_detail::operator_forwarders<T, protocol_view<const T>, true,
                                               true>::type::operator=;
};

// ---------------------------------------------------------------------------
// protocol_view<T> — the mutable view.
// ---------------------------------------------------------------------------

template <typename T>
class protocol_view
    : public reflection_detail::named_forwarders<T, protocol_view<T>, false,
                                                 true>::type,
      public reflection_detail::operator_forwarders<T, protocol_view<T>, false,
                                                    true>::type {
  template <typename>
  friend class protocol_view;
  template <typename, std::meta::info, std::meta::info, typename, bool, bool>
  friend struct reflection_detail::forwarding_call;
  template <typename, std::meta::info, typename, std::meta::operators, bool,
            bool>
  friend struct reflection_detail::operator_forwarder;

  using view_vtable = typename reflection_detail::view_vtable<T>::vtable;

  template <std::meta::info Member, typename... Arguments>
  decltype(auto) dispatch_reflected_member(Arguments&&... arguments) const {
    static_assert(
        reflection_detail::forwarders_at_offset_zero(^^protocol_view));
    constexpr std::meta::info entry = reflection_detail::data_member_named(
        ^^typename reflection_detail::view_vtable<T>::view_entries,
        reflection_detail::vtable_entry_name(Member));
    return vptr_->entries.[:entry:](ptr_,
                                    std::forward<Arguments>(arguments)...);
  }

  void* ptr_;
  const view_vtable* vptr_;

  template <typename Alloc>
  static void* checked_ptr(protocol<T, Alloc>& p) noexcept {
    assert(!p.valueless_after_move());
    return p.p_;
  }

 public:
  template <typename U>
    requires reflection_protocol_concept<U, T> && not_protocol_or_view<U>
  protocol_view(U& obj) noexcept
      : ptr_(std::addressof(obj)),
        vptr_(&reflection_detail::view_vtable_for<T, std::remove_cvref_t<U>>) {}

  template <typename U>
    requires reflection_protocol_concept<U, T> && not_protocol_or_view<U>
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

  // See protocol's own operator= for why this using-declaration is needed:
  // without it, the implicitly-declared copy-assignment operator this class
  // relies on would hide any operator= inherited from operator_forwarders.
  using reflection_detail::operator_forwarders<T, protocol_view<T>, false,
                                               true>::type::operator=;
};

template <typename T>
inline protocol_view<const T>::protocol_view(protocol_view<T> other) noexcept
    : ptr_(other.ptr_), vptr_(other.vptr_) {}

}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_H_

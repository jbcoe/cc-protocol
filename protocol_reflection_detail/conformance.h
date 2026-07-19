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
// Conformance checking for the C++26 reflection backend: does a stored
// implementation type satisfy an interface, member by member.
// Split out of protocol_reflection.h because this piece is self-contained
// (depends only on members.h and types.h, nothing from vtable-building,
// thunks, or forwarders) and independently testable: conformance_test.cc
// includes this header directly rather than the whole backend, checking the
// two public concepts against interface/implementation pairs with no
// xyz::protocol or real vtable involved.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_CONFORMANCE_H_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_CONFORMANCE_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <cstddef>
#include <meta>
#include <type_traits>
#include <utility>
#include <vector>

#include "protocol_reflection_detail/members.h"
#include "protocol_reflection_detail/types.h"

namespace xyz {
namespace reflection_detail {

using std::meta::info;

// ---------------------------------------------------------------------------
// Resolving an interface member against a stored implementation type
//
// Duck-typed dispatch: given interface member M and implementation type U,
// find the member(s) of U a call could target, by name (or operator kind)
// and constness only, with no parameter or return type filtering. Rather
// than hand-comparing signatures, which would mean reimplementing overload
// resolution, every matching candidate is wrapped and merged into one
// callable type via `using Candidates::operator()...`, the same idiom
// `overloaded_calls` (protocol_reflection.h) uses for an interface's own
// overload sets, applied here to the implementation's candidates instead.
// The compiler's real overload resolution then picks the match, for both
// the conformance check and the real call: the concept (below) checks not
// "is there an exact signature match" but "is this merged candidate set
// callable", exactly what the Python backend's requires-expression-based
// concept (a real call, e.g. `t.method(std::declval<Arg>()...)`) and Ryan
// Keane's rjk::duck technique (https://ryanjk5.github.io/posts/rjk-duck/)
// check.
// ---------------------------------------------------------------------------

consteval bool same_member_name(info interface_member,
                                info implementation_member) {
  if (std::meta::has_identifier(interface_member)) {
    return std::meta::has_identifier(implementation_member) &&
           std::meta::identifier_of(interface_member) ==
               std::meta::identifier_of(implementation_member);
  }
  return std::meta::is_operator_function(implementation_member) &&
         std::meta::operator_of(interface_member) ==
             std::meta::operator_of(implementation_member);
}

// Members of implementation_type matching interface_member by name (or
// operator kind), constness, and noexcept. A noexcept interface member can
// only ever be dispatched through a noexcept-qualified candidate: the vtable
// thunk and every forwarder are generated noexcept(true) for such a member
// (mirroring its exact declaration), so a throwing candidate is not merely
// non-conforming but would let an exception escape a noexcept function,
// terminating the program instead of failing to compile. is_invocable_r_v
// (used below to check the merge) does not itself inspect noexcept, so this
// filters candidates the same way the constness check above does, rather
// than relying on that trait to catch the mismatch.
//
// Rvalue-qualified candidates are excluded for an unrelated reason: self is
// always accessed as an lvalue here, and merging one in anyway would
// collide with a same-constness unqualified candidate's identical
// operator() signature (member_function_type discards ref-qualification),
// making the merge ambiguous rather than unusable.
consteval std::vector<info> resolve_implementation_candidates(
    info implementation_type, info interface_member) {
  std::vector<info> candidates;
  for (info candidate : std::meta::members_of(
           implementation_type, std::meta::access_context::current())) {
    if (!is_interface_member_function(candidate)) continue;
    if (!same_member_name(interface_member, candidate)) continue;
    if (std::meta::is_const(interface_member) &&
        !std::meta::is_const(candidate)) {
      continue;
    }
    if (std::meta::is_noexcept(interface_member) &&
        !std::meta::is_noexcept(candidate)) {
      continue;
    }
    if (std::meta::is_rvalue_reference_qualified(candidate)) continue;
    candidates.push_back(candidate);
  }
  return candidates;
}

// One matching implementation candidate, callable with its own real
// parameter types, not the interface member's. Merging several of these
// (candidate_overload_set, below) and calling the merge with the interface
// member's argument types is what routes the call through the compiler's
// real overload resolution, implicit conversions included, instead of a
// hand-rolled comparison.
//
// operator() is const-qualified exactly when Candidate itself is const, not
// based on the outer erasure (ConstErased, which only picks self's pointee
// constness). This matters when a stored type declares both a const and a
// non-const overload of the same name and parameters: merging two wrappers
// whose operator()s differ only by this same cv-qualification is ordinary
// C++ overloading on constness, exactly as if the class had declared both
// directly. Real overload resolution then ranks them the same way it would
// rank calling the member directly: preferring non-const on a non-const
// access path, with only the const one viable at all on a const path.
template <typename Implementation, info Candidate, typename Signature,
          bool ConstErased, bool CandidateIsConst>
struct implementation_candidate_call;

template <typename Implementation, info Candidate, typename ReturnType,
          typename... ParameterTypes, bool ConstErased>
struct implementation_candidate_call<Implementation, Candidate,
                                     ReturnType(ParameterTypes...), ConstErased,
                                     false> {
  using SelfPointer =
      std::conditional_t<ConstErased, const Implementation*, Implementation*>;
  SelfPointer self;

  explicit implementation_candidate_call(SelfPointer self) : self(self) {}

  ReturnType operator()(ParameterTypes... parameters) {
    return self->[:Candidate:](std::forward<ParameterTypes>(parameters)...);
  }
};

template <typename Implementation, info Candidate, typename ReturnType,
          typename... ParameterTypes, bool ConstErased>
struct implementation_candidate_call<Implementation, Candidate,
                                     ReturnType(ParameterTypes...), ConstErased,
                                     true> {
  using SelfPointer =
      std::conditional_t<ConstErased, const Implementation*, Implementation*>;
  SelfPointer self;

  explicit implementation_candidate_call(SelfPointer self) : self(self) {}

  ReturnType operator()(ParameterTypes... parameters) const {
    return self->[:Candidate:](std::forward<ParameterTypes>(parameters)...);
  }
};

// Merges one wrapper per implementation candidate for a single interface
// member. This is the implementation-candidate axis; overloaded_calls
// (protocol_reflection.h) is the unrelated interface-overload axis, merging
// wrappers for an interface's own declared overloads.
template <typename... Candidates>
struct candidate_overload_set : Candidates... {
  using Candidates::operator()...;

  template <typename SelfPointer>
  explicit candidate_overload_set(SelfPointer self) : Candidates(self)... {}
};

// The merged candidate_overload_set type (or the lone candidate's own
// wrapper type, or info{} if there are no name/constness-eligible
// candidates at all) for one interface member against implementation_type.
consteval info make_candidate_overload_set(info implementation_type,
                                           info interface_member,
                                           bool const_erased) {
  std::vector<info> candidates =
      resolve_implementation_candidates(implementation_type, interface_member);
  if (candidates.empty()) return info{};
  std::vector<info> wrapper_types;
  for (info candidate : candidates) {
    wrapper_types.push_back(std::meta::substitute(
        ^^implementation_candidate_call,
        {
            implementation_type, std::meta::reflect_constant(candidate),
            member_function_type(candidate),
            std::meta::reflect_constant(const_erased),
            std::meta::reflect_constant(std::meta::is_const(candidate))}));
  }
  if (wrapper_types.size() == 1) return wrapper_types.front();
  return std::meta::substitute(^^candidate_overload_set, wrapper_types);
}

// Peels R(Ps...) back into a real parameter pack, since is_invocable_r_v and
// is_invocable_v need a template argument pack, not a std::vector<info>, to
// check whether MergedCandidates is callable with an interface member's
// parameter types. Same partial-specialization idiom erased_call_thunk
// (thunk.h) uses.
template <info MergedCandidates, typename Signature>
struct is_invocable_with_return;

template <info MergedCandidates, typename ReturnType,
          typename... ParameterTypes>
struct is_invocable_with_return<MergedCandidates,
                                ReturnType(ParameterTypes...)> {
  static constexpr bool value =
      std::is_void_v<ReturnType>
          ? std::is_invocable_v<typename[:MergedCandidates:], ParameterTypes...>
          : std::is_invocable_r_v<
                ReturnType, typename[:MergedCandidates:], ParameterTypes...>;
};

template <typename Implementation, typename Interface, std::size_t Index>
consteval bool member_is_satisfiable(bool const_only) {
  constexpr info member = interface_members<Interface>[Index];
  if (const_only && !std::meta::is_const(member)) return true;
  constexpr info merged = make_candidate_overload_set(
      ^^Implementation, member, std::meta::is_const(member));
  if constexpr (merged == info{}) {
    return false;
  } else {
    return is_invocable_with_return<
        merged, typename[:member_function_type(member):]>::value;
  }
}

template <typename Implementation, typename Interface, std::size_t... Indexes>
consteval bool all_members_satisfiable(bool const_only,
                                       std::index_sequence<Indexes...>) {
  return (... && member_is_satisfiable<Implementation, Interface, Indexes>(
                     const_only));
}

template <typename Implementation, typename Interface>
consteval bool models_reflected_interface(bool const_only = false) {
  // Non-class candidates (e.g. an int offered to a converting constructor
  // during overload resolution) have no members to enumerate, so they don't
  // model the interface. This must be `if constexpr`, not a plain `if`:
  // all_members_satisfiable is a template, and merely naming it in a
  // live (non-discarded) statement forces its instantiation regardless of
  // which branch would run at evaluation time. That instantiation includes
  // make_candidate_overload_set's std::meta::members_of call on
  // Implementation. Only a discarded if-constexpr branch is skipped.
  if constexpr (!std::meta::is_class_type(
                    std::meta::dealias(^^Implementation)) ||
                !std::meta::is_complete_type(
                    std::meta::dealias(^^Implementation))) {
    return false;
  } else {
    return all_members_satisfiable<Implementation, Interface>(
        const_only,
        std::make_index_sequence<interface_members<Interface>.size()>());
  }
}

}  // namespace reflection_detail

// Reflection-backed equivalents of the per-interface concepts
// protocol_const_concept_<Name> / protocol_concept_<Name>.
template <typename Implementation, typename Interface>
concept reflection_protocol_const_concept =
    reflection_detail::models_reflected_interface<
        std::remove_cvref_t<Implementation>, Interface>(true);

template <typename Implementation, typename Interface>
concept reflection_protocol_concept =
    reflection_detail::models_reflected_interface<
        std::remove_cvref_t<Implementation>, Interface>(false);

}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_CONFORMANCE_H_

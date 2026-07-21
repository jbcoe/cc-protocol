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
// Requires GCC 16+ with `-std=c++26 -freflection`, and the build option
// XYZ_PROTOCOL_ENABLE_REFLECTION to be defined. On any other compiler or
// build configuration this header is an inert no-op. When the backend is
// active, protocol.h compiles out its placeholder primary templates so this
// header can define xyz::protocol / xyz::protocol_view as primary templates;
// protocol.h's public declarations are otherwise unchanged.
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
// - Interfaces with several assignment operators of the same constness are
//   not supported (assignment forwarding requires a unique target per
//   constness).
// - Generated member functions are not marked constexpr (out of scope for
//   this backend for now).
#ifndef XYZ_PROTOCOL_REFLECTION_H_
#define XYZ_PROTOCOL_REFLECTION_H_

#if defined(__cpp_impl_reflection) && defined(XYZ_PROTOCOL_ENABLE_REFLECTION)

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

namespace xyz {

namespace reflection_detail {

using std::meta::info;

// ---------------------------------------------------------------------------
// Interface member enumeration
// ---------------------------------------------------------------------------

// Member function templates are deliberately excluded: a template has no
// fixed signature, so it cannot be mapped to a single vtable slot. This is
// currently implied by std::meta::is_function returning false for an
// uninstantiated function template, but is checked explicitly so the
// exclusion doesn't depend on that incidental behaviour.
consteval bool is_interface_member_function(info member) {
  return std::meta::is_function(member) &&
         !std::meta::is_function_template(member) &&
         !std::meta::is_special_member_function(member) &&
         !std::meta::is_static_member(member) && std::meta::is_public(member) &&
         (std::meta::has_identifier(member) ||
          std::meta::is_operator_function(member));
}

// All interface member functions, in declaration order, optionally filtered
// by constness. Declaration order is load-bearing: vtable entry order and
// forwarder generation both follow it.
consteval std::vector<info> interface_member_functions(
    info interface_type, bool const_only = false) {
  std::vector<info> result;
  for (info member : std::meta::members_of(
           interface_type, std::meta::access_context::current())) {
    if (is_interface_member_function(member)) {
      if (!const_only || std::meta::is_const(member)) {
        result.push_back(member);
      }
    }
  }
  return result;
}

// Names that are always public members of protocol / protocol_view
// (see the class definitions below). An interface member function with one
// of these names would be silently hidden by C++ name-hiding rules rather
// than reachable through the generated forwarders, so it is rejected with a
// static_assert instead of compiling to a silently broken call.
consteval bool has_reserved_interface_member_name(info interface_type) {
  for (info member : interface_member_functions(interface_type)) {
    if (!std::meta::has_identifier(member)) continue;
    std::string_view name = std::meta::identifier_of(member);
    if (name == "swap" || name == "valueless_after_move") return true;
  }
  return false;
}

// The interface's member list, enumerated once per interface and persisted
// with define_static_array. Vtable construction (make_vtable_entry) and the
// concept checks (models_reflected_interface) index this array; forwarder
// generation (define_named_forwarders, make_operator_forwarders) instead
// re-enumerates via interface_member_functions directly.
template <typename Interface>
inline constexpr auto interface_members =
    std::define_static_array(interface_member_functions(^^Interface));

// ---------------------------------------------------------------------------
// Deterministic, stable entry naming
//
// Vtable entry names must be valid C++ identifiers, unique within one
// interface's vtable, and a deterministic function of a member's own
// signature -- not of its position among other members. The last part
// matters because narrowing (see "Vtable narrowing maps" below) matches
// vtable entries between two *different* interfaces by name, so the same
// signature must always produce the same name regardless of what other
// members its interface happens to declare.
//
// Rather than hashing the signature (which only gives a probabilistic
// guarantee against collisions), identifier_safe_string encodes it exactly:
// every byte outside [a-zA-Z0-9] -- including a literal `_` itself -- is
// replaced by `_` followed by its two-digit hex value. This is injective
// by construction: scanning left to right, a `_` in the output always
// starts a two-hex-digit escape, since no unescaped `_` from the input
// ever survives unescaped, so two different signatures can never collide
// the way two different hash inputs theoretically could.
// ---------------------------------------------------------------------------

consteval std::string identifier_safe_string(std::string_view text) {
  const char* hex_digits = "0123456789abcdef";
  std::string result;
  for (unsigned char byte : text) {
    bool is_identifier_safe = (byte >= 'a' && byte <= 'z') ||
                              (byte >= 'A' && byte <= 'Z') ||
                              (byte >= '0' && byte <= '9');
    if (is_identifier_safe) {
      result += static_cast<char>(byte);
    } else {
      result += '_';
      result += hex_digits[(byte >> 4) & 0xF];
      result += hex_digits[byte & 0xF];
    }
  }
  return result;
}

// Identifier-safe spelling for an operator kind, mirroring the intent of
// mangle_identifier in scripts/generate_protocol.py.
consteval std::string_view operator_spelling(std::meta::operators kind) {
  using enum std::meta::operators;
  switch (kind) {
    case op_parentheses:
      return "call";
    case op_square_brackets:
      return "subscript";
    case op_arrow:
      return "arrow";
    case op_arrow_star:
      return "arrow_star";
    case op_tilde:
      return "tilde";
    case op_exclamation:
      return "exclamation";
    case op_plus:
      return "plus";
    case op_minus:
      return "minus";
    case op_star:
      return "star";
    case op_slash:
      return "slash";
    case op_percent:
      return "percent";
    case op_caret:
      return "caret";
    case op_ampersand:
      return "ampersand";
    case op_equals:
      return "equals";
    case op_pipe:
      return "pipe";
    case op_plus_equals:
      return "plus_equals";
    case op_minus_equals:
      return "minus_equals";
    case op_star_equals:
      return "star_equals";
    case op_slash_equals:
      return "slash_equals";
    case op_percent_equals:
      return "percent_equals";
    case op_caret_equals:
      return "caret_equals";
    case op_ampersand_equals:
      return "ampersand_equals";
    case op_pipe_equals:
      return "pipe_equals";
    case op_equals_equals:
      return "equals_equals";
    case op_exclamation_equals:
      return "exclamation_equals";
    case op_less:
      return "less";
    case op_greater:
      return "greater";
    case op_less_equals:
      return "less_equals";
    case op_greater_equals:
      return "greater_equals";
    case op_spaceship:
      return "spaceship";
    case op_ampersand_ampersand:
      return "ampersand_ampersand";
    case op_pipe_pipe:
      return "pipe_pipe";
    case op_less_less:
      return "less_less";
    case op_greater_greater:
      return "greater_greater";
    case op_less_less_equals:
      return "less_less_equals";
    case op_greater_greater_equals:
      return "greater_greater_equals";
    case op_plus_plus:
      return "plus_plus";
    case op_minus_minus:
      return "minus_minus";
    case op_comma:
      return "comma";
    case op_new:
      return "new";
    case op_delete:
      return "delete";
    case op_array_new:
      return "array_new";
    case op_array_delete:
      return "array_delete";
    case op_co_await:
      return "co_await";
  }
  return "";
}

consteval std::string mangled_member_name(info member) {
  if (std::meta::has_identifier(member)) {
    return std::string(std::meta::identifier_of(member));
  }
  return "operator_" +
         std::string(operator_spelling(std::meta::operator_of(member)));
}

consteval std::string member_signature_string(info member) {
  std::string signature = mangled_member_name(member);
  signature += "(";
  bool first = true;
  for (info parameter : std::meta::parameters_of(member)) {
    if (!first) signature += ",";
    first = false;
    signature += std::meta::display_string_of(
        std::meta::dealias(std::meta::type_of(parameter)));
  }
  signature += ")";
  if (std::meta::is_const(member)) signature += " const";
  if (std::meta::is_noexcept(member)) signature += " noexcept";
  return signature;
}

consteval std::string vtable_entry_name(info member) {
  return identifier_safe_string(member_signature_string(member));
}

// ---------------------------------------------------------------------------
// Type synthesis helpers
// ---------------------------------------------------------------------------

template <typename ReturnType, typename... ParameterTypes>
using function_type_for = ReturnType(ParameterTypes...);

template <typename ReturnType, typename... ParameterTypes>
using function_pointer_type_for = ReturnType (*)(ParameterTypes...);

template <typename ReturnType, typename... ParameterTypes>
using noexcept_function_pointer_type_for =
    ReturnType (*)(ParameterTypes...) noexcept;

consteval std::vector<info> parameter_types_of(info member) {
  std::vector<info> result;
  for (info parameter : std::meta::parameters_of(member)) {
    result.push_back(std::meta::type_of(parameter));
  }
  return result;
}

// R(Ps...) for an interface member, used to pattern-match thunks and call
// wrappers so their signatures exactly mirror the interface declaration.
consteval info member_function_type(info member) {
  std::vector<info> arguments;
  arguments.push_back(std::meta::return_type_of(member));
  for (info parameter_type : parameter_types_of(member)) {
    arguments.push_back(parameter_type);
  }
  return std::meta::substitute(^^function_type_for, arguments);
}

// R(*)(ErasedPointer, Ps...) [noexcept] — the type of one vtable entry.
consteval info vtable_entry_pointer_type(info member,
                                         info erased_pointer_type) {
  std::vector<info> arguments;
  arguments.push_back(std::meta::return_type_of(member));
  arguments.push_back(erased_pointer_type);
  for (info parameter_type : parameter_types_of(member)) {
    arguments.push_back(parameter_type);
  }
  return std::meta::substitute(std::meta::is_noexcept(member)
                                   ? ^^noexcept_function_pointer_type_for
                                   : ^^function_pointer_type_for,
                               arguments);
}

consteval info data_member_named(info class_type, std::string_view name) {
  for (info member : std::meta::nonstatic_data_members_of(
           class_type, std::meta::access_context::current())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      return member;
    }
  }
  return info{};
}

// ---------------------------------------------------------------------------
// Resolving an interface member against a stored implementation type
//
// Duck-typed dispatch: given interface member M and implementation type U,
// find the member(s) of U a call could target, by name (or operator kind)
// and constness only -- no parameter or return type filtering here. Rather
// than hand-comparing signatures (which would mean reimplementing overload
// resolution), every matching candidate is wrapped and merged into one
// callable type via `using Candidates::operator()...` -- the same idiom
// `overloaded_calls` (below) uses for an interface's own overload sets,
// applied here to the implementation's candidates instead -- and the
// compiler's real overload resolution is what actually picks the match,
// for both the conformance check and the real call. This is what the
// concept (below) is checking: not "is there an exact signature match",
// but "is this merged candidate set genuinely callable" -- exactly what
// the Python backend's requires-expression-based concept (a real call,
// e.g. `t.method(std::declval<Arg>()...)`) and Ryan Keane's rjk::duck
// technique (https://ryanjk5.github.io/posts/rjk-duck/) already check.
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

// All members of implementation_type that could plausibly serve
// interface_member: same name (or operator kind), and constness-compatible
// (a const interface member can only ever be called through a const self,
// so it requires a const candidate; a non-const interface member may match
// either -- real overload resolution on a non-const self naturally prefers
// a non-const candidate over a const one, so no preference is tracked here).
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
    candidates.push_back(candidate);
  }
  return candidates;
}

// One matching implementation candidate, callable with its own real
// parameter types -- not the interface member's. Merging several of these
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
// C++ overloading on constness -- exactly as if the class had declared
// both directly -- so real overload resolution ranks them the same way it
// would rank calling the member directly (prefers non-const on a non-const
// access path; only the const one is even viable on a const path). Giving
// every wrapper the same qualification regardless of the candidate would
// make such a pair ambiguous instead of ranked.
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
// (below) is the unrelated interface-overload axis (merging wrappers for
// an interface's own declared overloads) -- the two must not be confused.
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

// Peels R(Ps...) back into a real parameter pack so is_invocable_r_v /
// is_invocable_v -- which need a template argument pack, not a
// std::vector<info> -- can check whether MergedCandidates is callable with
// an interface member's parameter types. Same partial-specialization idiom
// erased_call_thunk uses below.
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
  // during overload resolution) have no members to enumerate; they simply
  // don't model the interface. This must be `if constexpr`, not a plain
  // `if`: all_members_satisfiable is a template, and merely naming it in a
  // live (non-discarded) statement forces its instantiation -- including
  // evaluating make_candidate_overload_set's std::meta::members_of
  // call on Implementation -- regardless of which branch would run at
  // evaluation time. Only a discarded if-constexpr branch is skipped.
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

namespace reflection_detail {

// ---------------------------------------------------------------------------
// Erased-call thunks: the static functions vtable entries point at.
// Signature exactly mirrors the interface member (exact parameter types,
// matching noexcept), with a leading erased pointer. ConstErased selects the
// const_view flavour (const void* + const implementation access).
// MergedCandidates is the make_candidate_overload_set type built
// for this interface member: the thunk constructs an instance bound to
// self and calls through it, so it's the compiler's real overload
// resolution -- not this thunk -- that picks which candidate actually runs.
// ---------------------------------------------------------------------------

template <typename Implementation, info MergedCandidates, typename Signature,
          bool ConstErased, bool IsNoexcept>
struct erased_call_thunk;

template <typename Implementation, info MergedCandidates, typename ReturnType,
          typename... ParameterTypes, bool ConstErased, bool IsNoexcept>
struct erased_call_thunk<Implementation, MergedCandidates,
                         ReturnType(ParameterTypes...), ConstErased,
                         IsNoexcept> {
  using ErasedPointer = std::conditional_t<ConstErased, const void*, void*>;
  using SelfPointer =
      std::conditional_t<ConstErased, const Implementation*, Implementation*>;

  static ReturnType call(ErasedPointer erased,
                         ParameterTypes... parameters) noexcept(IsNoexcept) {
    auto* self = static_cast<SelfPointer>(erased);
    using Candidates = [:MergedCandidates:];
    Candidates candidates(self);
    if constexpr (std::is_void_v<ReturnType>) {
      candidates(std::forward<ParameterTypes>(parameters)...);
    } else {
      return candidates(std::forward<ParameterTypes>(parameters)...);
    }
  }
};

// ---------------------------------------------------------------------------
// Vtable layout per interface
//
// Each vtable is a handwritten shell (so it can carry typedefs and the fixed
// lifetime members, which define_aggregate cannot produce) containing a
// define_aggregate'd `entries` sub-aggregate with one function-pointer
// member per interface member, named by vtable_entry_name.
// ---------------------------------------------------------------------------

consteval void define_vtable_entries(info incomplete_entries_type,
                                     info interface_type) {
  std::vector<info> entry_specifications;
  for (info member : interface_member_functions(interface_type, false)) {
    info erased_pointer_type =
        std::meta::is_const(member) ? ^^const void* : ^^void*;
    entry_specifications.push_back(std::meta::data_member_spec(
        vtable_entry_pointer_type(member, erased_pointer_type),
        {.name = vtable_entry_name(member)}));
  }
  std::meta::define_aggregate(incomplete_entries_type, entry_specifications);
}

template <typename Interface>
struct view_vtable {
  struct view_entries;
  consteval { define_vtable_entries(^^view_entries, ^^Interface); }

  struct vtable {
    using xyz_reflection_view_vtable_tag = void;
    using protocol_type = Interface;
    view_entries entries;
  };
};

template <typename Interface, typename Allocator>
struct owning_vtable {
  struct owning_entries;
  consteval { define_vtable_entries(^^owning_entries, ^^Interface); }

  struct vtable {
    using xyz_reflection_owning_vtable_tag = void;
    using protocol_type = Interface;
    using allocator_type = Allocator;
    void* (*xyz_protocol_clone)(void* erased, const Allocator& allocator);
    void* (*xyz_protocol_move)(void* erased, const Allocator& allocator);
    void (*xyz_protocol_destroy)(void* erased, const Allocator& allocator);
    const typename view_vtable<Interface>::vtable* view_vt;
    owning_entries entries;
  };
};

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
// Vtable narrowing maps, found by ADL from protocol.h's get_vtable /
// get_mutable_vtable / get_owning_vtable. Entries are copied by name: every
// entry of the target vtable must exist, identically named (same signature
// hash), in the source vtable — which is exactly the subset relationship
// narrowing conversions rely on.
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
void map_vtable_members(const FromVtable* from, ToVtable* to) {
  copy_vtable_entries(from->entries, to->entries);
}

template <reflection_view_vtable FromVtable, reflection_view_vtable ToVtable>
void map_mutable_vtable_members(const FromVtable* from, ToVtable* to) {
  copy_vtable_entries(from->entries, to->entries);
}

template <reflection_owning_vtable FromVtable,
          reflection_owning_vtable ToVtable>
void map_owning_vtable_members(const FromVtable* from, ToVtable* to) {
  to->xyz_protocol_clone = from->xyz_protocol_clone;
  to->xyz_protocol_move = from->xyz_protocol_move;
  to->xyz_protocol_destroy = from->xyz_protocol_destroy;
  to->view_vt =
      get_mutable_vtable<typename FromVtable::protocol_type,
                         typename ToVtable::protocol_type>(from->view_vt);
  copy_vtable_entries(from->entries, to->entries);
}

// ---------------------------------------------------------------------------
// Forwarding call wrappers
//
// GCC 16 cannot splice a reflection as the declarator-id of a new member
// function, so per-method forwarders are synthesized as [[no_unique_address]]
// data members (named by the interface member's identifier) whose type
// overloads operator() with the exact interface signature. The wrapper
// recovers its owning protocol / protocol_view object via
// static_cast<Owner*>(static_cast<void*>(this)): every wrapper lives at offset
// zero inside an empty base of the owner, so the addresses coincide (checked
// with static_asserts at each use site). Overloaded interface names become one
// data member whose type merges one wrapper per overload.
// ---------------------------------------------------------------------------

template <typename Owner, info Member, typename Signature, bool ConstCall,
          bool IsNoexcept>
struct forwarding_call;

template <typename Owner, info Member, typename ReturnType,
          typename... ParameterTypes, bool IsNoexcept>
struct forwarding_call<Owner, Member, ReturnType(ParameterTypes...), false,
                       IsNoexcept> {
  ReturnType operator()(ParameterTypes... parameters) noexcept(IsNoexcept) {
    auto* owner = static_cast<Owner*>(static_cast<void*>(this));
    return owner->template dispatch_reflected_member<Member>(
        std::forward<ParameterTypes>(parameters)...);
  }
};

template <typename Owner, info Member, typename ReturnType,
          typename... ParameterTypes, bool IsNoexcept>
struct forwarding_call<Owner, Member, ReturnType(ParameterTypes...), true,
                       IsNoexcept> {
  ReturnType operator()(ParameterTypes... parameters) const
      noexcept(IsNoexcept) {
    const auto* owner =
        static_cast<const Owner*>(static_cast<const void*>(this));
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

// The empty base holding one forwarder data member per uniquely-named
// interface member (operators are handled separately, since a data member
// cannot be named `operator+`). ForceConstCall makes every wrapper's
// operator() const regardless of the interface member's constness, matching
// the generated protocol_view classes whose forwarders are all const.
consteval void define_named_forwarders(info incomplete_type, info owner_type,
                                       info interface_type, bool const_only,
                                       bool force_const_call) {
  std::vector<info> specifications;
  for (const std::vector<info>& overloads : grouped_interface_members(
           interface_member_functions(interface_type, const_only), false)) {
    std::vector<info> overload_wrappers;
    for (info member : overloads) {
      overload_wrappers.push_back(std::meta::substitute(
          ^^forwarding_call,
          {
              owner_type, std::meta::reflect_constant(member),
              member_function_type(member),
              std::meta::reflect_constant(force_const_call ||
                                          std::meta::is_const(member)),
              std::meta::reflect_constant(std::meta::is_noexcept(member))}));
    }
    info wrapper_type =
        overload_wrappers.size() == 1
            ? overload_wrappers.front()
            : std::meta::substitute(^^overloaded_calls, overload_wrappers);
    specifications.push_back(std::meta::data_member_spec(
        wrapper_type, {.name = std::meta::identifier_of(overloads.front()),
                       .no_unique_address = true}));
  }
  std::meta::define_aggregate(incomplete_type, specifications);
}

template <typename Interface, typename Owner, bool ConstOnly,
          bool ForceConstCall>
struct named_forwarders {
  struct type;
  consteval {
    define_named_forwarders(^^type, ^^Owner, ^^Interface, ConstOnly,
                            ForceConstCall);
  }
};

// ---------------------------------------------------------------------------
// Operator forwarding
//
// A data member cannot be named `operator+`, so interface operators cannot
// use the named-forwarder mechanism. Instead, for each operator kind a
// dedicated forwarder class template is stamped out below (the operator
// symbol must appear literally in source, hence one macro expansion per
// kind), and the per-interface set of operator forwarders is combined into
// an empty base class of protocol / protocol_view. operator= is the one
// exception: an inherited operator= is hidden by the implicitly-declared
// copy assignment operator, so assignment is forwarded by constrained
// member templates written directly in each class instead.
// ---------------------------------------------------------------------------

template <typename Owner, info Member, typename Signature,
          std::meta::operators Kind, bool ConstCall, bool IsNoexcept>
struct operator_forwarder;

// One join per operator kind gathers every overload of that kind into a
// single class: name lookup for e.g. `operator+` across multiple distinct
// bases would be ambiguous, so the overloads must be merged with
// using-declarations, which also require the operator symbol literally.
template <std::meta::operators Kind, typename... Forwarders>
struct operator_join;

#define XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(symbol, kind)      \
  template <typename Owner, info Member, typename ReturnType,                \
            typename... ParameterTypes, bool IsNoexcept>                     \
  struct operator_forwarder<Owner, Member, ReturnType(ParameterTypes...),    \
                            std::meta::operators::kind, false, IsNoexcept> { \
    ReturnType operator symbol(ParameterTypes... parameters) noexcept(       \
        IsNoexcept) {                                                        \
      auto* owner = static_cast<Owner*>(this);                               \
      return owner->template dispatch_reflected_member<Member>(              \
          std::forward<ParameterTypes>(parameters)...);                      \
    }                                                                        \
  };                                                                         \
  template <typename Owner, info Member, typename ReturnType,                \
            typename... ParameterTypes, bool IsNoexcept>                     \
  struct operator_forwarder<Owner, Member, ReturnType(ParameterTypes...),    \
                            std::meta::operators::kind, true, IsNoexcept> {  \
    ReturnType operator symbol(ParameterTypes... parameters) const           \
        noexcept(IsNoexcept) {                                               \
      const auto* owner = static_cast<const Owner*>(this);                   \
      return owner->template dispatch_reflected_member<Member>(              \
          std::forward<ParameterTypes>(parameters)...);                      \
    }                                                                        \
  };                                                                         \
  template <typename... Forwarders>                                          \
  struct operator_join<std::meta::operators::kind, Forwarders...>            \
      : Forwarders... {                                                      \
    using Forwarders::operator symbol...;                                    \
  };

// operator-> / operator~ / operator! grammatically take no parameter list at
// all — declaring them with an (always-empty) parameter pack is rejected —
// so they get a dedicated nullary stamp.
#define XYZ_PROTOCOL_REFLECTION_DEFINE_NULLARY_OPERATOR_FORWARDER(symbol,      \
                                                                  kind)        \
  template <typename Owner, info Member, typename ReturnType, bool IsNoexcept> \
  struct operator_forwarder<Owner, Member, ReturnType(),                       \
                            std::meta::operators::kind, false, IsNoexcept> {   \
    ReturnType operator symbol() noexcept(IsNoexcept) {                        \
      auto* owner = static_cast<Owner*>(this);                                 \
      return owner->template dispatch_reflected_member<Member>();              \
    }                                                                          \
  };                                                                           \
  template <typename Owner, info Member, typename ReturnType, bool IsNoexcept> \
  struct operator_forwarder<Owner, Member, ReturnType(),                       \
                            std::meta::operators::kind, true, IsNoexcept> {    \
    ReturnType operator symbol() const noexcept(IsNoexcept) {                  \
      const auto* owner = static_cast<const Owner*>(this);                     \
      return owner->template dispatch_reflected_member<Member>();              \
    }                                                                          \
  };                                                                           \
  template <typename... Forwarders>                                            \
  struct operator_join<std::meta::operators::kind, Forwarders...>              \
      : Forwarders... {                                                        \
    using Forwarders::operator symbol...;                                      \
  };

#define XYZ_PROTOCOL_REFLECTION_COMMA_SYMBOL ,

XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER((), op_parentheses)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER([], op_square_brackets)
XYZ_PROTOCOL_REFLECTION_DEFINE_NULLARY_OPERATOR_FORWARDER(->, op_arrow)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(->*, op_arrow_star)
XYZ_PROTOCOL_REFLECTION_DEFINE_NULLARY_OPERATOR_FORWARDER(~, op_tilde)
XYZ_PROTOCOL_REFLECTION_DEFINE_NULLARY_OPERATOR_FORWARDER(!, op_exclamation)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(+, op_plus)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(-, op_minus)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(*, op_star)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(/, op_slash)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(%, op_percent)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(^, op_caret)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(&, op_ampersand)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(|, op_pipe)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(+=, op_plus_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(-=, op_minus_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(*=, op_star_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(/=, op_slash_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(%=, op_percent_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(^=, op_caret_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(&=, op_ampersand_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(|=, op_pipe_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(==, op_equals_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(!=, op_exclamation_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(<, op_less)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(>, op_greater)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(<=, op_less_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(>=, op_greater_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(<=>, op_spaceship)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(&&, op_ampersand_ampersand)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(||, op_pipe_pipe)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(<<, op_less_less)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(>>, op_greater_greater)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(<<=, op_less_less_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(>>=,
                                                  op_greater_greater_equals)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(++, op_plus_plus)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(--, op_minus_minus)
XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER(
    XYZ_PROTOCOL_REFLECTION_COMMA_SYMBOL, op_comma)

#undef XYZ_PROTOCOL_REFLECTION_COMMA_SYMBOL
#undef XYZ_PROTOCOL_REFLECTION_DEFINE_NULLARY_OPERATOR_FORWARDER
#undef XYZ_PROTOCOL_REFLECTION_DEFINE_OPERATOR_FORWARDER

template <typename... Joins>
struct combined_operator_joins : Joins... {};

consteval info make_operator_forwarders(info interface_type, info owner_type,
                                        bool const_only,
                                        bool force_const_call) {
  std::vector<info> joins;
  for (const std::vector<info>& overloads : grouped_interface_members(
           interface_member_functions(interface_type, const_only), true)) {
    std::meta::operators kind = std::meta::operator_of(overloads.front());
    // operator= is forwarded by constrained member templates written
    // directly in each class (an inherited operator= is hidden by the
    // implicitly-declared copy assignment operator).
    if (kind == std::meta::operators::op_equals) continue;
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

// The interface assignment operator a call on a const (const_call == true)
// or non-const (const_call == false) protocol object should dispatch to. A
// const call can only use a const assignment operator. A non-const call
// prefers a unique non-const assignment operator and falls back to a unique
// const one only when the interface declares no non-const assignment
// operator at all, mirroring the constness preference of real overload
// resolution. Several assignment operators of the same constness have no
// unique target and remain unsupported (documented parity deviation).
consteval info assignment_operator_member(info interface_type,
                                          bool const_call) {
  info non_const_member{};
  int non_const_count = 0;
  info const_member{};
  int const_count = 0;
  for (info member : interface_member_functions(interface_type, false)) {
    if (std::meta::has_identifier(member) ||
        std::meta::operator_of(member) != std::meta::operators::op_equals) {
      continue;
    }
    if (std::meta::is_const(member)) {
      const_member = member;
      ++const_count;
    } else {
      non_const_member = member;
      ++non_const_count;
    }
  }
  if (const_call) return const_count == 1 ? const_member : info{};
  if (non_const_count == 1) return non_const_member;
  if (non_const_count == 0 && const_count == 1) return const_member;
  return info{};
}

template <typename Interface, bool ConstCall, typename Argument>
consteval bool assignment_operator_invocable() {
  info member = assignment_operator_member(^^Interface, ConstCall);
  if (member == info{}) return false;
  std::vector<info> parameters = parameter_types_of(member);
  if (parameters.size() != 1) return false;
  return std::meta::is_convertible_type(^^Argument, parameters[0]);
}

// Layout guard for the static_cast in the forwarders: every forwarder base
// subobject, every forwarder data member inside those bases (the named
// forwarding wrappers whose `this` is cast back to the owner), and their
// bases recursively must sit at offset zero of the owning object.
// [[no_unique_address]] is only a request, so the collapse to offset zero is
// asserted rather than assumed.
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
// Registry hookup: matches the shape expected by get_vtable /
// get_mutable_vtable / get_owning_vtable in protocol.h, so they work
// unmodified regardless of backend.
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
// protocol<T, Allocator> — primary template definition. The
// constructor/assignment/swap/destructor set mirrors the generated
// per-interface class in protocol.h, with the per-interface concepts and
// vtable types replaced by their reflection equivalents. Named-method
// forwarding comes from the named_forwarders empty base.
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
  template <typename, std::meta::info, typename, bool, bool>
  friend struct reflection_detail::forwarding_call;
  template <typename, std::meta::info, typename, std::meta::operators, bool,
            bool>
  friend struct reflection_detail::operator_forwarder;

  static_assert(!reflection_detail::has_reserved_interface_member_name(^^T),
                "xyz::protocol: interface must not declare a member function "
                "named 'swap' or 'valueless_after_move' - these names are "
                "reserved for protocol's own public members and would be "
                "silently hidden by ordinary C++ name-hiding rules");

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

  // Forwarders for an interface-declared operator= (which cannot come from
  // a base class: the copy assignment operator above would hide it).
  template <typename Argument>
    requires not_protocol_or_view<Argument> &&
             (reflection_detail::assignment_operator_invocable<T, false,
                                                               Argument>())
  decltype(auto) operator=(Argument&& argument) {
    constexpr std::meta::info member =
        reflection_detail::assignment_operator_member(^^T, false);
    return dispatch_reflected_member<member>(std::forward<Argument>(argument));
  }

  template <typename Argument>
    requires not_protocol_or_view<Argument> &&
             (reflection_detail::assignment_operator_invocable<T, true,
                                                               Argument>())
  decltype(auto) operator=(Argument&& argument) const {
    constexpr std::meta::info member =
        reflection_detail::assignment_operator_member(^^T, true);
    return dispatch_reflected_member<member>(std::forward<Argument>(argument));
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
  template <typename, std::meta::info, typename, bool, bool>
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
      : ptr_(other.ptr_), vptr_(get_vtable<Other, T>(other.vptr_)) {}

  template <typename Other>
    requires(!std::same_as<Other, T>)
  protocol_view(const protocol_view<Other>& other) noexcept
      : ptr_(other.ptr_), vptr_(get_vtable<Other, T>(other.vptr_)) {}

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

  template <typename Argument>
    requires not_protocol_or_view<Argument> &&
             (reflection_detail::assignment_operator_invocable<T, true,
                                                               Argument>())
  decltype(auto) operator=(Argument&& argument) const {
    constexpr std::meta::info member =
        reflection_detail::assignment_operator_member(^^T, true);
    return dispatch_reflected_member<member>(std::forward<Argument>(argument));
  }
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
  template <typename, std::meta::info, typename, bool, bool>
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
      : ptr_(other.ptr_), vptr_(get_mutable_vtable<Other, T>(other.vptr_)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(protocol<Other, Alloc>& p) noexcept
      : protocol_view(protocol_view<Other>(p)) {}

  template <typename Other, typename Alloc>
    requires(!std::same_as<Other, T>)
  protocol_view(protocol<Other, Alloc>&&) = delete;

  template <typename Argument>
    requires not_protocol_or_view<Argument> &&
             (reflection_detail::assignment_operator_invocable<T, false,
                                                               Argument>())
  decltype(auto) operator=(Argument&& argument) const {
    constexpr std::meta::info member =
        reflection_detail::assignment_operator_member(^^T, false);
    return dispatch_reflected_member<member>(std::forward<Argument>(argument));
  }
};

template <typename T>
inline protocol_view<const T>::protocol_view(protocol_view<T> other) noexcept
    : ptr_(other.ptr_), vptr_(other.vptr_) {}

}  // namespace xyz

#endif  // __cpp_impl_reflection && XYZ_PROTOCOL_ENABLE_REFLECTION
#endif  // XYZ_PROTOCOL_REFLECTION_H_

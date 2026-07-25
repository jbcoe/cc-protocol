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
// Duck-typed structural conformance: whether an implementation type
// provides what an interface's member functions need is decided by
// ordinary overload resolution among same-named candidates, each still
// required to return the interface member's own type exactly. Mirrors Ryan
// Keane's rjk::duck (https://ryanjk5.github.io/posts/rjk-duck/) and the
// Python/libclang backend's own per-member requires-expression concept
// (scripts/protocol.j2).
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_CONFORMANCE_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_CONFORMANCE_HXX_

#include <concepts>
#include <meta>
#include <string_view>
#include <vector>

#include "protocol_reflection_detail/members.hxx"
#include "protocol_reflection_detail/types.hxx"

namespace xyz::reflection_detail {

// Every member of `implementation_type` sharing `interface_member`'s name.
// No signature pre-filtering: which candidate, if any, a given call resolves
// to is for the compiler to decide, not this function.
consteval std::vector<std::meta::info> resolve_implementation_candidates(
    std::meta::info interface_member, std::meta::info implementation_type) {
  std::vector<std::meta::info> result;
  std::string_view name = std::meta::identifier_of(interface_member);
  for (std::meta::info member : std::meta::members_of(
           implementation_type, std::meta::access_context::current())) {
    if (std::meta::is_function(member) && std::meta::is_public(member) &&
        !std::meta::is_static_member(member) &&
        !std::meta::is_special_member_function(member) &&
        std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      result.push_back(member);
    }
  }
  return result;
}

// Whether `candidate` can be called as implementation.candidate(ps...)
// through a reference of ImplementationReference, returning exactly R:
// argument passing goes through ordinary overload resolution (implicit
// conversions allowed), but the return type must match R exactly, matching
// the Python/libclang backend's own concept (scripts/protocol.j2). A named
// variable template, not inlined into a requires-clause: splicing directly
// inside a requires-clause hits a GCC 16 trunk mangler limitation ("sorry,
// unimplemented: mangling splice_expr") once the enclosing function is
// called.
template <std::meta::info Candidate, typename ImplementationReference,
          typename R, typename... Ps>
constexpr bool candidate_is_callable_v =
    requires(ImplementationReference implementation, Ps... ps) {
      { implementation.[:Candidate:](ps...) } -> std::same_as<R>;
    };

// A concrete, non-generic forwarder for one candidate: its operator() has
// that candidate's own parameter types rather than a forwarding template, so
// overload resolution across several forwarders (candidate_overload_set)
// ranks them the way it would rank the candidates themselves. A generic
// forwarding template can't be ranked against other equally-generic
// templates and produces ambiguity instead of picking the best match.
//
// operator() is constrained, not just declared. A compound-requirement like
// `{ implementation.[:Candidate:](ps...) } -> std::same_as<R>` only checks
// that some viable declaration exists, not that the one selected has a
// well-formed body: calling a non-const candidate through a const
// ImplementationReference still type-checks as a declaration. The
// constraint removes this operator() from the overload set when Candidate
// can't be invoked through ImplementationReference (wrong constness, most
// commonly), instead of hard-erroring once its body is instantiated.
template <std::meta::info Candidate, typename ImplementationReference,
          typename R, typename... Ps>
struct single_candidate_forwarder {
  R operator()(ImplementationReference implementation, Ps... ps) const
    requires candidate_is_callable_v<Candidate, ImplementationReference, R,
                                     Ps...>
  {
    return implementation.[:Candidate:](ps...);
  }
};

consteval std::meta::info single_candidate_forwarder_type(
    std::meta::info candidate, std::meta::info implementation_reference_type) {
  std::vector<std::meta::info> args{
      std::meta::reflect_constant(candidate), implementation_reference_type,
      std::meta::dealias(std::meta::return_type_of(candidate))};
  for (std::meta::info parameter_type : parameter_types_of(candidate)) {
    args.push_back(parameter_type);
  }
  return std::meta::substitute(^^single_candidate_forwarder, args);
}

// Merges N candidates' forwarders into one callable type via
// `using Forwarders::operator()...`, exactly rjk::duck's technique: which
// forwarder, if any, a given call resolves to is decided by the compiler's
// ordinary overload resolution.
template <typename... Forwarders>
struct candidate_overload_set : Forwarders... {
  using Forwarders::operator()...;
};

consteval std::meta::info candidate_overload_set_type(
    const std::vector<std::meta::info>& candidates,
    std::meta::info implementation_reference_type) {
  std::vector<std::meta::info> forwarder_types;
  for (std::meta::info candidate : candidates) {
    forwarder_types.push_back(single_candidate_forwarder_type(
        candidate, implementation_reference_type));
  }
  return std::meta::substitute(^^candidate_overload_set, forwarder_types);
}

// Same exact-return-type requirement as candidate_is_callable_v above,
// applied to the merged candidate overload set instead of one candidate.
template <typename Merged, typename ImplementationReference, typename R,
          typename... Ps>
constexpr bool implementation_candidate_call_v =
    requires(Merged merged, ImplementationReference implementation, Ps... ps) {
      { merged(implementation, ps...) } -> std::same_as<R>;
    };

// Whether `interface_member` is modeled by `implementation_type`, called
// through a reference of `implementation_reference_type`: resolves that
// member's name to candidates, merges them, and checks the merge is
// callable with `interface_member`'s own signature.
consteval bool member_is_modeled(
    std::meta::info interface_member, std::meta::info implementation_type,
    std::meta::info implementation_reference_type) {
  std::vector<std::meta::info> candidates =
      resolve_implementation_candidates(interface_member, implementation_type);
  if (candidates.empty()) return false;
  std::meta::info merged_type =
      candidate_overload_set_type(candidates, implementation_reference_type);
  std::vector<std::meta::info> args{
      merged_type, implementation_reference_type,
      std::meta::dealias(std::meta::return_type_of(interface_member))};
  for (std::meta::info parameter_type : parameter_types_of(interface_member)) {
    args.push_back(parameter_type);
  }
  return std::meta::extract<bool>(
      std::meta::substitute(^^implementation_candidate_call_v, args));
}

// Whether every (const-only, if ConstOnly) member function of Interface is
// modeled by Implementation. ConstOnly selects the subset a
// protocol_view<const Interface> (or a const Interface access path) can
// call: only Interface's own const member functions, each checked through
// a const Implementation reference regardless of that member's own
// constness in Interface.
template <typename Implementation, typename Interface, bool ConstOnly>
consteval bool models_reflected_interface() {
  for (std::meta::info member : interface_member_functions(^^Interface)) {
    if (ConstOnly && !std::meta::is_const(member)) continue;
    bool need_const = ConstOnly || std::meta::is_const(member);
    std::meta::info implementation_reference_type =
        need_const ? std::meta::add_lvalue_reference(
                         std::meta::add_const(^^Implementation))
                   : std::meta::add_lvalue_reference(^^Implementation);
    if (!member_is_modeled(member, ^^Implementation,
                           implementation_reference_type)) {
      return false;
    }
  }
  return true;
}

template <typename Implementation, typename Interface>
concept reflection_protocol_concept =
    models_reflected_interface<Implementation, Interface, false>();

template <typename Implementation, typename Interface>
concept reflection_protocol_const_concept =
    models_reflected_interface<Implementation, Interface, true>();

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_CONFORMANCE_HXX_

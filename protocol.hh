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
// Both `protocol` and `protocol_view` dispatch member function calls at
// runtime through a generated vtable; `protocol`'s vtable extends
// `protocol_view`'s with destroy/copy/move entries used for allocator-aware
// ownership.
//
// Neither implementation currently supports operators other than operator().

#include <algorithm>
#include <cassert>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <meta>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
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

// Per ISO C++ ([expr.prim.lambda.closure]), closure types are unique, unnamed,
// non-union class types.
// This concept will also match an unnamed class type with a single
// `operator()`.
// TODO(jbcoe): Refine this concept to match only lambdas.
template <typename T>
concept is_maybe_lambda =
    is_class_type(dealias(^^T)) && !has_identifier(dealias(^^T)) &&
    requires { &T::operator(); };

consteval bool is_call_operator(std::meta::info function) {
  return is_operator_function(function) &&
         operator_of(function) == std::meta::operators::op_parentheses;
}

// Returns `true` if `a` and `b` are both call operators or share an
// identifier.
consteval bool same_name(std::meta::info a, std::meta::info b) {
  if (is_call_operator(a) || is_call_operator(b))
    return is_call_operator(a) && is_call_operator(b);
  return has_identifier(a) && has_identifier(b) &&
         identifier_of(a) == identifier_of(b);
}

// A valid identifier for `member`, for naming generated vtable entries.
consteval std::string_view member_name(std::meta::info member) {
  return is_call_operator(member) ? "call_operator" : identifier_of(member);
}

// Returns `true` if the member functions `candidate` and `interface` have
// the same name, de-aliased return type and de-aliased parameter types.
consteval bool same_name_and_parameters(std::meta::info candidate,
                                        std::meta::info interface) {
  if (!same_name(candidate, interface)) return false;
  if (dealias(return_type_of(interface)) != dealias(return_type_of(candidate)))
    return false;
  auto dealiased_type_of = [](std::meta::info parameter) {
    return dealias(type_of(parameter));
  };
  return std::ranges::equal(parameters_of(interface), parameters_of(candidate),
                            {}, dealiased_type_of, dealiased_type_of);
}

// Returns `true` if the member functions `candidate` and `interface` have
// the same name, reference qualifiers, de-aliased return type and de-aliased
// parameter types; const and noexcept are not compared.
consteval bool same_signature_ignoring_const(std::meta::info candidate,
                                             std::meta::info interface) {
  if (is_lvalue_reference_qualified(interface) !=
      is_lvalue_reference_qualified(candidate))
    return false;
  if (is_rvalue_reference_qualified(interface) !=
      is_rvalue_reference_qualified(candidate))
    return false;
  return same_name_and_parameters(candidate, interface);
}

// Returns `true` if the `candidate` member function is consistent with the
// `interface` member function for the purposes of structural subtyping;
// otherwise returns `false`.
consteval bool member_function_conforms_to(std::meta::info candidate,
                                           std::meta::info interface) {
  if (is_static_member(candidate)) {
    // A static candidate has no object parameter, so it satisfies any const
    // or reference qualification of `interface`.
    if (!same_name_and_parameters(candidate, interface)) return false;
  } else {
    if (!same_signature_ignoring_const(candidate, interface)) return false;
    // If interface is `const`, `candidate` must be const.
    if (is_const(interface) != is_const(candidate)) return false;
  }
  // If interface is `noexcept`, `candidate` must be noexcept.
  return !is_noexcept(interface) || is_noexcept(candidate);
}

// The named, non-special member functions and call operators of `Type`,
// static or not, in declaration order: the members that can satisfy an
// interface member function.
template <std::meta::info Type>
consteval auto conformance_candidate_infos() {
  auto named = members_of(Type, std::meta::access_context::unprivileged()) |
               std::views::filter(std::meta::is_function) |
               std::views::filter([](std::meta::info member) consteval {
                 return has_identifier(member) || is_call_operator(member);
               });
  std::vector<std::meta::info> result(std::ranges::begin(named),
                                      std::ranges::end(named));
  // Per [meta.reflection.member.queries], a closure type's function call
  // operator is members-of-eligible, but GCC16's `members_of` does not yet
  // enumerate it, leaving `result` empty for lambdas; name the operator
  // directly as a fallback.
  using T = typename[:Type:];
  if constexpr (is_maybe_lambda<T>) {
    if (result.empty()) result.push_back(^^T::operator());
  }
  return result;
}

template <std::meta::info Type>
constexpr inline auto conformance_candidates_of =
    std::define_static_array(conformance_candidate_infos<Type>());

// The non-static members of `conformance_candidates_of<Type>`: the member
// functions an interface `Type` requires. Overloads appear as separate
// entries; an entry's index identifies its vtable slot.
template <std::meta::info Type>
constexpr inline auto protocol_interface_functions_of =
    std::define_static_array(
        conformance_candidates_of<Type> |
        std::views::filter(std::not_fn(std::meta::is_static_member)));

// The vtable entry for the interface member function at `Index`;
// `generate_vtable_specs` declares one entry per interface member function in
// the same order as `protocol_interface_functions_of`.
template <std::meta::info VtableType, std::size_t Index>
consteval std::meta::info vtable_entry_at() {
  return nonstatic_data_members_of(
      VtableType, std::meta::access_context::unprivileged())[Index];
}

// Vanishing-this-pointer thunk for one overload of a synthesised member
// function.
//
// The thunk carries a single operator() whose signature mirrors one method
// of the Interface type. `member_thunk` combines the thunks for all
// overloads of a name into one overload set.
template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
          typename Vtable, std::size_t Index, bool IsConst, bool IsNoexcept>
struct method_thunk;

// TODO(jbcoe): Extend this approach to handle lvalue and rvalue qualifiers.
template <typename R, typename... Args, typename EnclosingType,
          typename ProtocolType, typename Vtable, std::size_t Index,
          bool IsConst, bool IsNoexcept>
struct method_thunk<R (*)(Args...), EnclosingType, ProtocolType, Vtable, Index,
                    IsConst, IsNoexcept> {
  static constexpr std::meta::info vtable_entry =
      vtable_entry_at<^^Vtable, Index>();

  // Recovers the EnclosingType pointer: `call_operator_base` derives from
  // this thunk, while a generated `member_base` holds it as its sole data
  // member (the vanishing-this-pointer cast).
  template <typename Self>
  static auto* enclosing(Self* self) {
    using Enclosing = std::conditional_t<std::is_const_v<Self>,
                                         const EnclosingType, EnclosingType>;
    if constexpr (std::derived_from<EnclosingType, method_thunk>) {
      return static_cast<Enclosing*>(self);
    } else {
      return reinterpret_cast<Enclosing*>(self);
    }
  }

  // Provides member-function call syntax. Widens the EnclosingType pointer
  // to the enclosing protocol/protocol_view object, then calls through its
  // stored vtable pointer's matching function pointer, passing the
  // viewed/owned object.
  R operator()(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* protocol_object = static_cast<ProtocolType*>(enclosing(this));
    if constexpr (is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    constexpr std::meta::info entry = vtable_entry;
    return vtable->[:entry:](protocol_object->object_,
                             std::forward<Args>(args)...);
  }

  R operator()(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* protocol_object =
        static_cast<const ProtocolType*>(enclosing(this));
    if constexpr (is_protocol_v<ProtocolType>) {
      assert(!protocol_object->valueless_after_move() &&
             "cannot call member function of valueless protocol");
    }

    const Vtable* vtable = protocol_object->vtable_;
    constexpr std::meta::info entry = vtable_entry;
    return vtable->[:entry:](protocol_object->object_,
                             std::forward<Args>(args)...);
  }

 protected:
  // Only `member_thunk` may create or copy a thunk.
  method_thunk() = default;
  ~method_thunk() = default;
  method_thunk(const method_thunk&) = default;
  method_thunk(method_thunk&&) = default;
  method_thunk& operator=(const method_thunk&) = default;
  method_thunk& operator=(method_thunk&&) = default;
};

template <bool Noexcept, typename R, typename... Args>
using fn_ptr_t = R (*)(Args...) noexcept(Noexcept);

// One overload of a synthesised member function: the interface member, its
// vtable slot and the const-qualification of the generated wrapper.
template <std::meta::info Member, std::size_t Index, bool IsConst>
struct overload_spec {};

// The `method_thunk` specialisation for an `overload_spec`.
template <typename Spec, typename EnclosingType, typename ProtocolType,
          typename Vtable>
struct method_thunk_for;

template <std::meta::info Member, std::size_t Index, bool IsConst,
          typename EnclosingType, typename ProtocolType, typename Vtable>
struct method_thunk_for<overload_spec<Member, Index, IsConst>, EnclosingType,
                        ProtocolType, Vtable> {
  // Build the function-pointer type R(*)(Args...) from the method's return
  // type and parameter types.
  static consteval std::meta::info fn_ptr_type() {
    std::vector<std::meta::info> fn_args{std::meta::reflect_constant(false),
                                         dealias(return_type_of(Member))};
    fn_args.append_range(parameters_of(Member) |
                         std::views::transform(std::meta::type_of));
    return substitute(^^fn_ptr_t, fn_args);
  }

  // clang-format off
  using type = typename[:substitute(
      ^^method_thunk, {fn_ptr_type(), ^^EnclosingType, ^^ProtocolType, ^^Vtable,
                       std::meta::reflect_constant(Index),
                       std::meta::reflect_constant(IsConst),
                       std::meta::reflect_constant(is_noexcept(Member))}):];
  // clang-format on
};

template <typename Spec, typename EnclosingType, typename ProtocolType,
          typename Vtable>
using method_thunk_t =
    method_thunk_for<Spec, EnclosingType, ProtocolType, Vtable>::type;

// The overload set for one synthesised member function: a `method_thunk` per
// overload, with every operator() brought into scope so that overload
// resolution among them works as for a member function of the interface.
template <typename EnclosingType, typename ProtocolType, typename Vtable,
          typename... Specs>
struct member_thunk
    : method_thunk_t<Specs, EnclosingType, ProtocolType, Vtable>... {
  using method_thunk_t<Specs, EnclosingType, ProtocolType,
                       Vtable>::operator()...;

 private:
  friend EnclosingType;
  member_thunk() = default;
  ~member_thunk() = default;
  member_thunk(const member_thunk&) = default;
  member_thunk(member_thunk&&) = default;
  member_thunk& operator=(const member_thunk&) = default;
  member_thunk& operator=(member_thunk&&) = default;
};

// The wrapper base for an interface's call operators. Deriving from the
// overload set gives `p(args)` call syntax; protected special members let a
// protocol/protocol_view copy the base but stop it being sliced off.
template <typename ProtocolType, typename Vtable, typename... Specs>
struct call_operator_base
    : member_thunk<call_operator_base<ProtocolType, Vtable, Specs...>,
                   ProtocolType, Vtable, Specs...> {
 protected:
  call_operator_base() = default;
  ~call_operator_base() = default;
  call_operator_base(const call_operator_base&) = default;
  call_operator_base(call_operator_base&&) = default;
  call_operator_base& operator=(const call_operator_base&) = default;
  call_operator_base& operator=(call_operator_base&&) = default;
};

// How generated wrappers treat the const-qualification of interface members.
enum class const_policy {
  // `protocol<I>`: as declared in `I`, so `const protocol<I>` exposes only the
  // const member functions of `I` (const propagates).
  propagate,
  // `protocol_view<I>`: every wrapper is const-qualified regardless of `I`
  // (shallow const, as for `std::span`).
  all_const,
  // `protocol_view<const I>`: only the const member functions of `I`.
  const_only,
};

// Returns `true` if `ConstPolicy` generates a wrapper for `member`.
//
// Under `all_const` every wrapper is const, so a const/non-const overload
// pair `R f() const; R f();` would collide; the const overload is dropped as
// a non-const reference to `I` would also resolve `f()` to `R f()`.
template <const_policy ConstPolicy>
consteval bool generates_wrapper_for(std::meta::info member,
                                     std::span<const std::meta::info> members) {
  switch (ConstPolicy) {
    case const_policy::propagate:
      return true;
    case const_policy::const_only:
      return is_const(member);
    case const_policy::all_const:
      return !is_const(member) ||
             std::ranges::none_of(members, [&](std::meta::info other) {
               return !is_const(other) &&
                      same_signature_ignoring_const(other, member);
             });
  }
  std::unreachable();
}

// A single-member base wrapping the overload set for one interface member
// function name, named after that method (giving the `p.method_name(args)`
// call syntax). `Member` is the first overload and supplies the name; `Specs`
// are the `overload_spec`s of the overloads exposed by the `const_policy`.
template <std::meta::info Member, typename ProtocolType, typename Vtable,
          typename... Specs>
struct member_base_generator {
  struct member_base;
  consteval {
    // clang-format off
    std::meta::info thunk_type = substitute(
        ^^member_thunk, {^^member_base, ^^ProtocolType, ^^Vtable, ^^Specs...});

    define_aggregate(
      ^^member_base, {data_member_spec(thunk_type,
                             std::meta::data_member_options{
                              .name = identifier_of(Member),
                              .no_unique_address = true
                            })});
    // clang-format on
  }
};

template <std::meta::info Member, typename ProtocolType, typename Vtable,
          typename... Specs>
using member_base_generator_t =
    member_base_generator<Member, ProtocolType, Vtable, Specs...>::member_base;

// Combines the single-member base types produced by `member_base_generator`
// into one type via multiple inheritance.
template <typename... MemberBases>
struct wrapper_bases : MemberBases... {};

// Returns a `wrapper_bases` specialisation with one base per public,
// non-special, member function name of `interface_type`, giving named members
// with an `operator()` for each overload selected by `ConstPolicy`, plus a
// `call_operator_base` if `interface_type` has call operators.
template <std::meta::info InterfaceType, typename ProtocolType, typename Vtable,
          const_policy ConstPolicy>
consteval std::meta::info generate_wrapper_bases() {
  std::span<const std::meta::info> members =
      protocol_interface_functions_of<InterfaceType>;
  std::vector<std::meta::info> member_base_types;
  std::vector<std::meta::info> names_generated;
  for (std::meta::info first : members) {
    if (std::ranges::any_of(names_generated, [&](std::meta::info generated) {
          return same_name(first, generated);
        }))
      continue;
    names_generated.push_back(first);

    std::vector<std::meta::info> specs;
    for (std::size_t index = 0; index < members.size(); ++index) {
      std::meta::info member = members[index];
      if (!same_name(member, first) ||
          !generates_wrapper_for<ConstPolicy>(member, members))
        continue;
      const bool wrapper_is_const =
          ConstPolicy == const_policy::propagate ? is_const(member) : true;
      // clang-format off
      specs.push_back(substitute(
          ^^overload_spec, {reflect_constant(member),
                            std::meta::reflect_constant(index),
                            std::meta::reflect_constant(wrapper_is_const)}));
      // clang-format on
    }
    if (specs.empty()) continue;

    std::vector<std::meta::info> generator_args;
    if (!is_call_operator(first)) {
      generator_args.push_back(reflect_constant(first));
    }
    generator_args.push_back(^^ProtocolType);
    generator_args.push_back(^^Vtable);
    generator_args.append_range(specs);
    member_base_types.push_back(
        is_call_operator(first)
            ? substitute(^^call_operator_base, generator_args)
            : dealias(substitute(^^member_base_generator_t, generator_args)));
  }
  return substitute(^^wrapper_bases, member_base_types);
}

// The generated wrapper type for `T`: a `wrapper_bases` specialisation with
// named members with `operator()` for each public, non-special, member
// function from `T` selected by `ConstPolicy`.
template <typename T, typename ProtocolType, typename Vtable,
          const_policy ConstPolicy = const_policy::propagate>
using protocol_wrappers_t =
    typename[:generate_wrapper_bases<^^T, ProtocolType, Vtable,
                                     ConstPolicy>():];

// Names the vtable entry for the interface member function at `index`.
// Overloads share an identifier, so the index keeps entry names unique.
consteval std::string vtable_entry_name(std::meta::info member,
                                        std::size_t index) {
  char digits[std::numeric_limits<std::size_t>::digits10 + 1];
  auto [end, error] = std::to_chars(digits, digits + sizeof(digits), index);
  std::string name(member_name(member));
  name += '_';
  name.append(digits, end);
  return name;
}

// Returns a list of data_member_spec values, one for each member function
// implemented by `protocol`, each describing a vtable function pointer with
// signature R(*)(void*, Args...) for a mutable interface method, or
// R(*)(const void*, Args...) for a const one.
template <std::meta::info interface_type>
consteval std::vector<std::meta::info> generate_vtable_specs() {
  std::vector<std::meta::info> function_pointer_specs;

  std::span<const std::meta::info> members =
      protocol_interface_functions_of<interface_type>;
  for (std::size_t index = 0; index < members.size(); ++index) {
    std::meta::info member = members[index];

    // Build the function-pointer type R(*)(void*, Args...) noexcept(...)
    // from the method's return type, parameter types and noexcept-ness; a
    // const method takes `const void*` instead, matching the constness of
    // the access path it's called through.
    std::vector<std::meta::info> fn_args{
        std::meta::reflect_constant(is_noexcept(member)),
        dealias(return_type_of(member))};
    fn_args.push_back(is_const(member) ? ^^const void* : ^^void*);
    std::vector<std::meta::info> member_parameters = parameters_of(member);
    for (std::meta::info parameter : member_parameters) {
      fn_args.push_back(dealias(type_of(parameter)));
    }
    std::meta::info fn_ptr_type = substitute(^^fn_ptr_t, fn_args);

    function_pointer_specs.push_back(data_member_spec(
        fn_ptr_type, std::meta::data_member_options{
                         .name = vtable_entry_name(member, index)}));
  }
  return function_pointer_specs;
}

// Generates a vtable with named function pointers for each public,
// non-special, member function from `T`.
template <typename T>
struct vtable_generator {
  struct vtable;
  consteval { define_aggregate(^^vtable, generate_vtable_specs<^^T>()); }
};

// Finds the member of `CandidateType` that structurally conforms to
// `Member`, using the same matching rule as is_protocol_conformant.
template <std::meta::info Member, std::meta::info CandidateType>
consteval std::meta::info find_conforming_member() {
  for (std::meta::info candidate : conformance_candidates_of<CandidateType>) {
    if (member_function_conforms_to(candidate, Member)) return candidate;
  }
  std::unreachable();
}

// Recovers a `U*`/`const U*` from the type-erased pointer a vtable entry is
// called with, then calls the matching member of `U`.
template <typename FnPtrType, typename U, std::meta::info CandidateMember>
struct mutable_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember>
struct mutable_view_trampoline<R (*)(void*, Args...) noexcept(Noexcept), U,
                               CandidateMember> {
  static R call(void* ptr, Args... args) noexcept(Noexcept) {
    return static_cast<U*>(ptr)->[:CandidateMember:](
        std::forward<Args>(args)...);
  }
};

template <typename FnPtrType, typename U, std::meta::info CandidateMember>
struct const_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember>
struct const_view_trampoline<R (*)(const void*, Args...) noexcept(Noexcept), U,
                             CandidateMember> {
  static R call(const void* ptr, Args... args) noexcept(Noexcept) {
    return static_cast<const U*>(ptr)->[:CandidateMember:](
        std::forward<Args>(args)...);
  }
};

// Builds a vtable for `T` whose entries call through to the corresponding
// member of `U`.
//
// For `const_policy::propagate` (`protocol<I>`) and `const_policy::all_const`
// (`protocol_view<T>`) every entry is populated: `protocol` stores a decayed,
// non-const `TNorm`, and the view's constructor only accepts a non-const U
// (see its `!std::is_const_v<U>` constraint), so a sound pointer to call any
// member, const or mutating, through is always available.
//
// For `const_policy::const_only` (`protocol_view<const T>`) only the entries
// for const members of `T` are populated; the view generates no wrapper for
// the others, so they are never called.
template <typename T, typename U, const_policy ConstPolicy>
consteval typename vtable_generator<T>::vtable make_view_vtable() {
  using Vtable = typename vtable_generator<T>::vtable;
  Vtable result{};

  constexpr std::span<const std::meta::info> members =
      protocol_interface_functions_of<^^T>;
  template for (constexpr std::size_t index :
                std::views::iota(std::size_t{0}, members.size())) {
    constexpr std::meta::info member = members[index];
    constexpr std::meta::info vtable_member =
        vtable_entry_at<^^Vtable, index>();
    using FnPtrType = typename[:type_of(vtable_member):];
    if constexpr (is_const(member)) {
      constexpr std::meta::info candidate =
          find_conforming_member<member, ^^U>();
      result.[:vtable_member:] = &const_view_trampoline<FnPtrType, U,
                                                        candidate>::call;
    } else if constexpr (ConstPolicy != const_policy::const_only) {
      constexpr std::meta::info candidate =
          find_conforming_member<member, ^^U>();
      result.[:vtable_member:] = &mutable_view_trampoline<FnPtrType, U,
                                                          candidate>::call;
    }
  }
  return result;
}

// The shared, compile-time vtable every protocol_view<T> (or
// protocol_view<const T>, per `ConstPolicy`) that views a `U` points to.
template <typename T, typename U, const_policy ConstPolicy>
inline constexpr typename vtable_generator<T>::vtable view_vtable_for =
    make_view_vtable<T, U, ConstPolicy>();

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
      detail::protocol_interface_functions_of<^^Interface>;
  auto candidate_member_functions =
      detail::conformance_candidates_of<^^Candidate>;

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
//
// Owning, allocator-aware, value-semantic. Member functions are forwarded
// through the owning vtable (see `vtable` below). Calling a member function
// on a valueless (moved-from) `protocol` is a precondition violation.
// ---------------------------------------------------------------------------
template <typename I, typename Alloc = std::allocator<std::byte>>
class protocol
    : public detail::protocol_wrappers_t<
          I, protocol<I, Alloc>, typename detail::vtable_generator<I>::vtable> {
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

  using view_vtable = typename detail::vtable_generator<I>::vtable;

  // Extends the generated per-member-function vtable with the entries needed
  // for ownership. Because it derives from `view_vtable`, the synthesised
  // member function thunks (which take a `const view_vtable*`) can call
  // through a `const vtable*` unchanged.
  struct vtable : view_vtable {
    void (*destroy)(const Alloc& alloc, void* data);
    void* (*copy)(const Alloc& alloc, const void* data);
    void* (*move)(const Alloc& alloc, void* data);
  };

  // Builds the vtable for the stored type `TNorm`: the member function
  // entries call through to `TNorm`'s conforming member functions and the
  // ownership entries use the (rebound) allocator.
  template <typename T, typename TNorm = std::decay_t<T>>
  static consteval vtable make_vtable_for() {
    vtable result{};
    static_cast<view_vtable&>(result) =
        detail::make_view_vtable<I, TNorm, detail::const_policy::propagate>();

    result.destroy = +[](const Alloc& alloc, void* data) -> void {
      rebound<TNorm> new_alloc{alloc};
      auto* typed = static_cast<TNorm*>(data);
      rebound_traits<TNorm>::destroy(new_alloc, typed);
      rebound_traits<TNorm>::deallocate(new_alloc, typed, 1);
    };

    // Copy construction and assignment should only reach this
    // if the interface is copy constructible.
    result.copy = +[](const Alloc& alloc, const void* data) -> void* {
      if constexpr (std::is_copy_constructible_v<I>) {
        return create<TNorm>(alloc, *static_cast<const TNorm*>(data));
      } else {
        std::unreachable();
      }
    };

    result.move = +[](const Alloc& alloc, void* data) -> void* {
      return create<TNorm>(alloc, std::move(*static_cast<TNorm*>(data)));
    };

    return result;
  }

  // Creates a vtable for the type T. TNorm is used throughout
  // this file to create a convenient alias for a decayed type.
  template <typename T>
  static constexpr vtable vtable_for = make_vtable_for<T>();

  // A no-op vtable that is the stand-in for a nullptr vtable. Prevents
  // redundant null checks in the special member functions. The member
  // function entries are left null: calling a member function on a
  // valueless protocol is a precondition violation.
  static consteval vtable make_null_vtable() {
    vtable result{};
    result.destroy = +[](const Alloc&, void*) -> void {};
    result.copy = +[](const Alloc&, const void*) -> void* { return nullptr; };
    result.move = +[](const Alloc&, void*) -> void* { return nullptr; };
    return result;
  }

  static constexpr vtable null_vtable = make_null_vtable();

  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::size_t Index, bool IsConst, bool IsNoexcept>
  friend struct detail::method_thunk;

  [[no_unique_address]] Alloc alloc_;

  void* object_ = nullptr;
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
        object_(create<T>(alloc_, std::forward<T>(obj))),
        vtable_(&vtable_for<T>) {}

  // In-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place construction from conforming T.
  template <typename T, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>, Args&&... args)
      : alloc_(a),
        object_(create<T>(alloc_, std::forward<Args>(args)...)),
        vtable_(&vtable_for<T>) {}

  // In-place construction needs this overload because templates cannot
  // deduce initializer lists.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T> &&
             std::default_initializable<Alloc>)
  constexpr explicit protocol(std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : protocol(std::allocator_arg, Alloc{}, std::in_place_type<T>, il,
                 std::forward<Args>(args)...) {}

  // Allocator-aware in-place init-list construction from conforming T.
  template <typename T, typename U, typename... Args>
    requires(!std::same_as<std::decay_t<T>, protocol> &&
             is_protocol_conformant_v<I, T>)
  constexpr explicit protocol(std::allocator_arg_t, const Alloc& a,
                              std::in_place_type_t<T>,
                              std::initializer_list<U> il, Args&&... args)
      : alloc_(a),
        object_(create<T>(alloc_, il, std::forward<Args>(args)...)),
        vtable_(&vtable_for<T>) {}

  constexpr ~protocol() { vtable_->destroy(alloc_, object_); }

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
        object_(other.vtable_->copy(alloc_, other.object_)),
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
      object_ = other.object_;
    } else {
      // Slow path, we have to heap allocate and move construct.
      object_ = other.vtable_->move(alloc_, other.object_);
      other.vtable_->destroy(other.alloc_, other.object_);
    }

    other.object_ = nullptr;
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
      void* new_object = other.vtable_->copy(other.alloc_, other.object_);

      vtable_->destroy(alloc_, object_);
      object_ = new_object;
      alloc_ = other.alloc_;
    } else {
      void* new_object = other.vtable_->copy(alloc_, other.object_);
      vtable_->destroy(alloc_, object_);
      object_ = new_object;
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
      vtable_->destroy(alloc_, object_);
      object_ = other.object_;
      if constexpr (pocma) {
        alloc_ = other.alloc_;
      }
    } else {
      // Slow path: heap construct and move the object directly. Allocate first
      // for strong exception safety.
      void* new_object = other.vtable_->move(alloc_, other.object_);
      vtable_->destroy(alloc_, object_);
      other.vtable_->destroy(other.alloc_, other.object_);

      object_ = new_object;
    }

    other.object_ = nullptr;
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
    swap(object_, other.object_);
    swap(vtable_, other.vtable_);
  }

  // Can be discovered by ADL for more optimal swapping than std::swap.
  friend constexpr void swap(protocol& lhs,
                             protocol& rhs) noexcept(always_equal || pocs) {
    return lhs.swap(rhs);
  }

  constexpr const Alloc& get_allocator() const { return alloc_; }

  constexpr bool valueless_after_move() const { return object_ == nullptr; }
};

// ---------------------------------------------------------------------------
// protocol_view<T>
//
// A non-owning reference with shallow const: every member function of `T` is
// exposed as a const member function of the view, so `const protocol_view<T>`
// does not restrict the interface. Use `protocol_view<const T>` to view a
// const object.
// ---------------------------------------------------------------------------
template <typename T>
class protocol_view
    : public detail::protocol_wrappers_t<
          T, protocol_view<T>, typename detail::vtable_generator<T>::vtable,
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

  // Construct from any non-const type U that conforms to the Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  explicit protocol_view(U& object)
      : object_(static_cast<void*>(std::addressof(object))),
        vtable_(
            &detail::view_vtable_for<T, U, detail::const_policy::all_const>) {}

 private:
  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::size_t Index, bool IsConst, bool IsNoexcept>
  friend struct detail::method_thunk;

  // Non-owning pointer to the viewed object.
  void* object_ = nullptr;

  const typename detail::vtable_generator<T>::vtable* vtable_;
};

// ---------------------------------------------------------------------------
// protocol_view<const T>
//
// Views a (possibly const) object and exposes only the const member
// functions of `T`.
// ---------------------------------------------------------------------------
template <typename T>
class protocol_view<const T> : public detail::protocol_wrappers_t<
                                   T, protocol_view<const T>,
                                   typename detail::vtable_generator<T>::vtable,
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

  // Construct from any (possibly const) type U that conforms to the
  // Interface T.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>)
  explicit protocol_view(const U& object)
      : object_(static_cast<const void*>(std::addressof(object))),
        vtable_(
            &detail::view_vtable_for<T, U, detail::const_policy::const_only>) {}

 private:
  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::size_t Index, bool IsConst, bool IsNoexcept>
  friend struct detail::method_thunk;

  // Non-owning pointer to the viewed object.
  const void* object_ = nullptr;

  const typename detail::vtable_generator<T>::vtable* vtable_;
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_HH_

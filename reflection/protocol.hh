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
// protocol_view has a minimal but working implementation: functions are
// dispatched at runtime using a vtable.
//
// protocol currently supports only compile-time signature checks.
//
// Neither implementation currently supports overloaded member functions or
// operators.

#include <algorithm>
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
//
// TODO(jbcoe): Handle static functions as they can be used to satisfy
// interface conformance.
template <std::meta::info Type>
constexpr inline auto protocol_interface_functions_of =
    std::define_static_array(
        members_of(Type, std::meta::access_context::unprivileged()) |
        std::views::filter(std::meta::is_function) |
        std::views::filter(std::not_fn(std::meta::is_static_member)) |
        std::views::filter(std::meta::has_identifier));

// Finds the vtable_generator<T>::vtable data member with the same name as
// `Member`. vtable_generator and generate_wrapper_bases enumerate the same
// interface members using the same identifier, so a match always exists.
// `VtableType`/`Member` are template parameters for the same reason as
// protocol_interface_functions_of's `Type`.
template <std::meta::info VtableType, std::meta::info Member>
consteval std::meta::info find_vtable_member() {
  std::string_view target_name = identifier_of(Member);
  for (std::meta::info m :
       members_of(VtableType, std::meta::access_context::unprivileged())) {
    if (has_identifier(m) && identifier_of(m) == target_name) return m;
  }
  std::unreachable();
}

// ---------------------------------------------------------------------------
// Vanishing-this-pointer thunk for a synthesised member function.
//
// The thunk carries a single operator() whose signature mirrors one method
// of the Interface type.
// ---------------------------------------------------------------------------
template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
          typename Vtable, std::meta::info Member, bool IsConst,
          bool IsNoexcept>
struct method_thunk;

// TODO(jbcoe): Extend this approach to handle lvalue and rvalue qualifiers.
template <typename R, typename... Args, typename EnclosingType,
          typename ProtocolType, typename Vtable, std::meta::info Member,
          bool IsConst, bool IsNoexcept>
struct method_thunk<R (*)(Args...), EnclosingType, ProtocolType, Vtable, Member,
                    IsConst, IsNoexcept> {
  static consteval std::meta::info vtable_entry() {
    return find_vtable_member<^^Vtable, Member>();
  }

  // Provides member-function call syntax. Recovers the EnclosingType pointer
  // through the vanishing-this-pointer cast, widens it to the enclosing
  // protocol/protocol_view object, then calls through its stored vtable
  // pointer's matching function pointer, passing the viewed/owned object
  // (not the protocol/protocol_view wrapper itself).
  R operator()(Args... args) noexcept(IsNoexcept)
    requires(!IsConst)
  {
    auto* enclosing = reinterpret_cast<EnclosingType*>(this);
    auto* protocol_object = static_cast<ProtocolType*>(enclosing);
    const Vtable* vtable = protocol_object->vtable_;
    constexpr std::meta::info entry = vtable_entry();
    return (*vtable).[:entry:](protocol_object->object_, args...);
  }

  R operator()(Args... args) const noexcept(IsNoexcept)
    requires(IsConst)
  {
    const auto* enclosing = reinterpret_cast<const EnclosingType*>(this);
    const auto* protocol_object = static_cast<const ProtocolType*>(enclosing);
    const Vtable* vtable = protocol_object->vtable_;
    constexpr std::meta::info entry = vtable_entry();
    // IsConst is true on this overload, so vtable_generator generated this
    // entry with a leading `const void*` parameter; object_ (a plain
    // void*) converts to that implicitly.
    return (*vtable).[:entry:](protocol_object->object_, args...);
  }
};

template <bool Noexcept, typename R, typename... Args>
using fn_ptr_t = R (*)(Args...) noexcept(Noexcept);

// A single-member base wrapping the thunk for one interface member function,
// named after that method (giving the `p.method_name(args)` call syntax).
// `ProtocolType` and `Vtable` are threaded through to `method_thunk`; see its
// comment for why they can't be recovered from `member_base` itself.
template <std::meta::info Member, typename ProtocolType, typename Vtable>
struct member_base_generator {
  struct member_base;
  consteval {
    std::string_view name = identifier_of(Member);

    // Build the function-pointer type R(*)(Args...) from the method's
    // return type and parameter types.
    std::vector<std::meta::info> fn_args{std::meta::reflect_constant(false),
                                         dealias(return_type_of(Member))};
    std::vector<std::meta::info> member_parameters = parameters_of(Member);
    fn_args.append_range(member_parameters |
                         std::views::transform(std::meta::type_of));
    std::meta::info fn_ptr_type = substitute(^^fn_ptr_t, fn_args);

    // clang-format off
    std::meta::info thunk_type = substitute(
        ^^method_thunk, {fn_ptr_type, ^^member_base, ^^ProtocolType, ^^Vtable,
                       std::meta::reflect_constant(Member),
                       std::meta::reflect_constant(is_const(Member)),
                       std::meta::reflect_constant(is_noexcept(Member))});

    define_aggregate(
      ^^member_base, {data_member_spec(thunk_type,
                             std::meta::data_member_options{
                              .name = name,
                              .no_unique_address = true
                            })});
    // clang-format on
  }
};

template <std::meta::info Member, typename ProtocolType, typename Vtable>
using member_base_generator_t =
    member_base_generator<Member, ProtocolType, Vtable>::member_base;

// Combines the single-member base types produced by `member_base_generator`
// into one type via multiple inheritance.
template <typename... MemberBases>
struct wrapper_bases : MemberBases... {};

// Returns a `wrapper_bases` specialisation with one base per public,
// non-special, member function of `interface_type`, giving named members
// with `operator()` for each. `ProtocolType`/`Vtable` are forwarded to
// `member_base_generator`.
//
// Two bases defining a member of the same name make that name ambiguous to
// look up through the derived class, so overloaded methods are unsupported
// for now.
template <std::meta::info InterfaceType, typename ProtocolType, typename Vtable>
consteval std::meta::info generate_wrapper_bases() {
  std::vector<std::meta::info> member_base_types;
  for (std::meta::info member :
       protocol_interface_functions_of<InterfaceType>) {
    // clang-format off
    member_base_types.push_back(dealias(
        substitute(^^member_base_generator_t,
                   {reflect_constant(member), ^^ProtocolType, ^^Vtable}))
      );
    // clang-format on
  }
  return substitute(^^wrapper_bases, member_base_types);
}

// The generated wrapper type for `T`: a `wrapper_bases` specialisation with
// named members with `operator()` for each public, non-special, member
// function from `T`. `ProtocolType` is the enclosing protocol/protocol_view
// specialisation (protocol<T, Allocator> or protocol_view<T>) and `Vtable`
// is its vtable_generator<T>::vtable: each thunk needs both to reach
// ProtocolType's `vtable_` pointer and call through it.
template <typename T, typename ProtocolType, typename Vtable>
using protocol_wrappers_t =
    typename[:generate_wrapper_bases<^^T, ProtocolType, Vtable>():];

// ---------------------------------------------------------------------------
// Returns a list of data_member_spec values, one for each member function
// implemented by `protocol`, each describing a vtable function pointer with
// signature R(*)(void*, Args...) for a mutable interface method, or
// R(*)(const void*, Args...) for a const one.
//
// Because C++ disallows two data members with the same name inside the same
// class, overloaded methods will cause compile-time errors.
// We will address this limitation in a follow-up PR.
// ---------------------------------------------------------------------------
consteval std::vector<std::meta::info> generate_vtable_specs(
    std::meta::info interface_type) {
  std::vector<std::meta::info> function_pointer_specs;

  std::ranges::range auto members =
      std::define_static_array(members_of(
          interface_type, std::meta::access_context::unprivileged())) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::not_fn(std::meta::is_static_member)) |
      std::views::filter(std::meta::has_identifier);

  for (std::meta::info member : members) {
    std::string_view name = identifier_of(member);

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
        fn_ptr_type, std::meta::data_member_options{.name = name}));
  }
  return function_pointer_specs;
}

// Generates a vtable with named function pointers for each public,
// non-special, member function from `T`.
template <typename T>
struct vtable_generator {
  struct vtable;
  consteval { define_aggregate(^^vtable, generate_vtable_specs(^^T)); }
};

// Finds the member of `CandidateType` that structurally conforms to
// `Member`, using the same matching rule as is_protocol_conformant.
template <std::meta::info Member, std::meta::info CandidateType>
consteval std::meta::info find_conforming_member() {
  for (std::meta::info candidate :
       protocol_interface_functions_of<CandidateType>) {
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
    return static_cast<U*>(ptr)->[:CandidateMember:](args...);
  }
};

template <typename FnPtrType, typename U, std::meta::info CandidateMember>
struct const_view_trampoline;

template <typename R, typename... Args, bool Noexcept, typename U,
          std::meta::info CandidateMember>
struct const_view_trampoline<R (*)(const void*, Args...) noexcept(Noexcept), U,
                             CandidateMember> {
  static R call(const void* ptr, Args... args) noexcept(Noexcept) {
    return static_cast<const U*>(ptr)->[:CandidateMember:](args...);
  }
};

// Builds a vtable for `T` whose entries call through to the corresponding
// member of `U`. Every entry is populated: protocol_view's constructor only
// accepts a non-const U (see its `!std::is_const_v<U>` constraint), so a
// sound pointer to call any member, const or mutating, through is always
// available.
template <typename T, typename U>
consteval typename vtable_generator<T>::vtable make_view_vtable() {
  using Vtable = typename vtable_generator<T>::vtable;
  Vtable result{};

  template for (constexpr std::meta::info member :
                protocol_interface_functions_of<^^T>) {
    constexpr std::meta::info candidate = find_conforming_member<member, ^^U>();
    constexpr std::meta::info vtable_member =
        find_vtable_member<^^Vtable, member>();
    using FnPtrType = typename[:type_of(vtable_member):];
    if constexpr (is_const(member)) {
      result.[:vtable_member:] = &const_view_trampoline<FnPtrType, U,
                                                        candidate>::call;
    } else {
      result.[:vtable_member:] = &mutable_view_trampoline<FnPtrType, U,
                                                          candidate>::call;
    }
  }
  return result;
}

// The shared, compile-time vtable every protocol_view<T> that views a `U`
// points to.
template <typename T, typename U>
inline constexpr typename vtable_generator<T>::vtable view_vtable_for =
    make_view_vtable<T, U>();

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
      detail::protocol_interface_functions_of<^^Candidate>;

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
class protocol : public detail::protocol_wrappers_t<
                     T, protocol<T, Allocator>,
                     typename detail::vtable_generator<T>::vtable> {
 public:
  protocol() = delete;  // Deleted as `T` is used as an interface type.

  protocol(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol(protocol&&);  // Unconstrained.

  protocol& operator=(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol& operator=(protocol&&);  // Unconstrained.

  ~protocol();  // Unconstrained.

  // Construct from any type `U` that conforms to the interface `T`.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_v<std::remove_cvref_t<U>>)
  explicit protocol(U&& value);

  // Construct a `U` in place from `Ts...`, where `U` conforms to the interface
  // `T`.
  template <typename U, typename... Ts>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
             (!is_protocol_v<std::remove_cvref_t<U>>)
  explicit protocol(std::in_place_type_t<U>, Ts&&... ts);

 private:
  // Grants the synthesised member thunks access to `vtable_` so they can
  // locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member, bool IsConst,
            bool IsNoexcept>
  friend struct detail::method_thunk;

  const typename detail::vtable_generator<T>::vtable* vtable_;
};

template <typename T>
class protocol_view
    : public detail::protocol_wrappers_t<
          T, protocol_view<T>, typename detail::vtable_generator<T>::vtable> {
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
  // U being const is rejected unconditionally, regardless of whether T
  // actually declares any non-const methods: a simple, T-independent rule
  // is easier to reason about than one that only rejects const U when it
  // would actually be unsound.
  template <typename U>
    requires is_protocol_conformant_v<T, std::remove_cvref_t<U>> &&
                 (!is_protocol_view_v<std::remove_cvref_t<U>>) &&
                 (!std::is_const_v<U>)
  explicit protocol_view(U& object)
      : object_(static_cast<void*>(std::addressof(object))),
        vtable_(&detail::view_vtable_for<T, U>) {}

 private:
  // Grants the synthesised member thunks access to `object_`/`vtable_` so
  // they can locate and call through the matching vtable entry.
  template <typename FnPtrType, typename EnclosingType, typename ProtocolType,
            typename Vtable, std::meta::info Member, bool IsConst,
            bool IsNoexcept>
  friend struct detail::method_thunk;

  // Non-owning pointer to the viewed object.
  void* object_ = nullptr;

  const typename detail::vtable_generator<T>::vtable* vtable_;
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_HH_

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
#ifndef XYZ_PROTOCOL_CONVERSION_THUNKS_HH_
#define XYZ_PROTOCOL_CONVERSION_THUNKS_HH_

#include <meta>

#include "overload_spec.hh"
#include "vtable.hh"

namespace xyz::detail {

// The target type of the conversion function `Member`.
template <std::meta::info Member>
using conversion_target_t = typename[:dealias(return_type_of(Member)):];

// Thunk for one overload of a synthesised conversion function. A conversion
// function can't be reached through a named member, so
// `conversion_overload_set` derives from this thunk directly instead of
// holding it as a data member, letting `ProtocolType` be recovered with a
// plain static_cast.
template <typename ProtocolType, typename Vtable, std::meta::info Member,
          member_options Options>
struct conversion_thunk {
  explicit(is_explicit(Member)) operator conversion_target_t<Member>() noexcept(
      is_noexcept(Member))
    requires(Options == member_options::none)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(protocol_object);
  }

  explicit(is_explicit(Member)) operator conversion_target_t<Member>() const
      noexcept(is_noexcept(Member))
    requires(Options == member_options::is_const)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(protocol_object);
  }

  explicit(is_explicit(Member))
  operator conversion_target_t<Member>() & noexcept(is_noexcept(Member))
    requires(Options == member_options::is_lvalue)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(protocol_object);
  }

  explicit(is_explicit(Member))
  operator conversion_target_t<Member>() && noexcept(is_noexcept(Member))
    requires(Options == member_options::is_rvalue)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(protocol_object);
  }

  explicit(is_explicit(Member)) operator conversion_target_t<Member>()
      const& noexcept(is_noexcept(Member))
    requires(Options == (member_options::is_const | member_options::is_lvalue))
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(protocol_object);
  }

  explicit(is_explicit(Member)) operator conversion_target_t<Member>()
      const&& noexcept(is_noexcept(Member))
    requires(Options == (member_options::is_const | member_options::is_rvalue))
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(protocol_object);
  }

 protected:
  conversion_thunk() = default;
  ~conversion_thunk() = default;
  conversion_thunk(const conversion_thunk&) = default;
  conversion_thunk(conversion_thunk&&) = default;
  conversion_thunk& operator=(const conversion_thunk&) = default;
  conversion_thunk& operator=(conversion_thunk&&) = default;
};

// The `conversion_thunk` for an `overload_spec`.
template <typename OverloadSpec, typename ProtocolType, typename Vtable>
struct conversion_thunk_for;

template <std::meta::info Member, member_options Options, typename ProtocolType,
          typename Vtable>
struct conversion_thunk_for<overload_spec<Member, Options>, ProtocolType,
                            Vtable> {
  using type = conversion_thunk<ProtocolType, Vtable, Member, Options>;
};

template <typename Spec, typename ProtocolType, typename Vtable>
using conversion_thunk_t =
    conversion_thunk_for<Spec, ProtocolType, Vtable>::type;

// The overload set for one synthesised conversion target: a
// `conversion_thunk` per overload converting to `TargetType`. Distinct
// conversion targets on one interface don't hide each other (each has a
// distinct conversion-function-id), so this only ever needs to merge
// overloads that share `TargetType`.
template <typename TargetType, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct conversion_overload_set
    : conversion_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using conversion_thunk_t<OverloadSpecs, ProtocolType,
                           Vtable>::operator TargetType...;

 protected:
  conversion_overload_set() = default;
  ~conversion_overload_set() = default;
  conversion_overload_set(const conversion_overload_set&) = default;
  conversion_overload_set(conversion_overload_set&&) = default;
  conversion_overload_set& operator=(const conversion_overload_set&) = default;
  conversion_overload_set& operator=(conversion_overload_set&&) = default;
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_CONVERSION_THUNKS_HH_

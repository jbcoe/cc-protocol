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

#include "xyz/detail/overload_spec.hh"
#include "xyz/detail/vtable.hh"

namespace xyz::detail {

// The target type of the conversion function `Member`.
template <std::meta::info Member>
using conversion_target_t = typename[:dealias(return_type_of(Member)):];

// Thunk for one overload of a synthesised conversion function. A conversion
// function can't be reached through a named member, so
// `xyz_protocol_conversion_overload_set` derives from this thunk directly
// instead of holding it as a data member, letting `ProtocolType` be recovered
// with a plain static_cast.
template <typename ProtocolType, typename Vtable, std::meta::info Member,
          member_qualifiers MemberQualifiers>
struct xyz_protocol_conversion_thunk {
  explicit(is_explicit(Member)) operator conversion_target_t<Member>() noexcept(
      is_noexcept(Member))
    requires(MemberQualifiers == member_qualifiers::unqualified)
  {
    auto* protocol_object = static_cast<ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->xyz_protocol_vtable_,
        protocol_object->xyz_protocol_erased_object());
  }

  explicit(is_explicit(Member)) operator conversion_target_t<Member>() const
      noexcept(is_noexcept(Member))
    requires(MemberQualifiers == member_qualifiers::const_qualified)
  {
    const auto* protocol_object = static_cast<const ProtocolType*>(this);
    return call_through_vtable<Member, Vtable>(
        protocol_object, protocol_object->xyz_protocol_vtable_,
        protocol_object->xyz_protocol_erased_object());
  }

 protected:
  xyz_protocol_conversion_thunk() = default;
  ~xyz_protocol_conversion_thunk() = default;
  xyz_protocol_conversion_thunk(const xyz_protocol_conversion_thunk&) = default;
  xyz_protocol_conversion_thunk(xyz_protocol_conversion_thunk&&) = default;
  xyz_protocol_conversion_thunk& operator=(
      const xyz_protocol_conversion_thunk&) = default;
  xyz_protocol_conversion_thunk& operator=(xyz_protocol_conversion_thunk&&) =
      default;
};

// The `xyz_protocol_conversion_thunk` for an `overload_spec`.
template <typename OverloadSpec, typename ProtocolType, typename Vtable>
struct conversion_thunk_for;

template <std::meta::info Member, member_qualifiers MemberQualifiers,
          typename ProtocolType, typename Vtable>
struct conversion_thunk_for<overload_spec<Member, MemberQualifiers>,
                            ProtocolType, Vtable> {
  using type = xyz_protocol_conversion_thunk<ProtocolType, Vtable, Member,
                                             MemberQualifiers>;
};

template <typename Spec, typename ProtocolType, typename Vtable>
using conversion_thunk_t =
    conversion_thunk_for<Spec, ProtocolType, Vtable>::type;

// The overload set for one synthesised conversion target: a
// `xyz_protocol_conversion_thunk` per overload converting to `TargetType`.
// Distinct conversion targets on one interface don't hide each other (each has
// a distinct conversion-function-id), so this only ever needs to merge
// overloads that share `TargetType`.
template <typename TargetType, typename ProtocolType, typename Vtable,
          typename... OverloadSpecs>
struct xyz_protocol_conversion_overload_set
    : conversion_thunk_t<OverloadSpecs, ProtocolType, Vtable>... {
  using conversion_thunk_t<OverloadSpecs, ProtocolType,
                           Vtable>::operator TargetType...;

 protected:
  xyz_protocol_conversion_overload_set() = default;
  ~xyz_protocol_conversion_overload_set() = default;
  xyz_protocol_conversion_overload_set(
      const xyz_protocol_conversion_overload_set&) = default;
  xyz_protocol_conversion_overload_set(xyz_protocol_conversion_overload_set&&) =
      default;
  xyz_protocol_conversion_overload_set& operator=(
      const xyz_protocol_conversion_overload_set&) = default;
  xyz_protocol_conversion_overload_set& operator=(
      xyz_protocol_conversion_overload_set&&) = default;
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_CONVERSION_THUNKS_HH_

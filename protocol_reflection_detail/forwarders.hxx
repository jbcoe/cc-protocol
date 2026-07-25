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
// Generalizes a one-member forwarder base to a reflection-computed number
// of members. A Wrapper here recovers its owner by casting through the
// one-member base forwarder_base gives it, then a base-to-derived
// static_cast to the owner. This file only assembles the bases; a
// Wrapper's own behavior and its owner's type are defined elsewhere, by
// whatever combines these bases into a concrete forwarding type.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_FORWARDERS_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_FORWARDERS_HXX_

#include <meta>
#include <vector>

#include "protocol_reflection_detail/naming.hxx"

namespace xyz::reflection_detail {

// A struct equivalent to (for e.g. Wrapper = GreetWrapper, Member =
// ^^Greeter::name):
//   struct Incomplete { [[no_unique_address]] GreetWrapper name; };
// Member is a template parameter rather than a function parameter because
// consteval blocks can't use local variables or parameters, only template
// parameters, and define_aggregate must run from one.
template <typename Wrapper, std::meta::info Member>
consteval std::meta::info make_forwarder_base_info() {
  struct Incomplete;
  consteval {
    define_aggregate(^^Incomplete,
                     {
                         std::meta::data_member_spec(
                             ^^Wrapper, {
                                            .name = vtable_entry_name(Member),
                                            .no_unique_address = true})});
  }
  return ^^Incomplete;
}

template <typename Wrapper, std::meta::info Member>
using forwarder_base = typename[:make_forwarder_base_info<Wrapper, Member>():];

// Combines N forwarder_base specializations into one type via ordinary
// multiple inheritance, giving their eventual owner every named forwarder
// at once with no splicing at any call site.
template <typename... Bases>
struct forwarders : Bases... {};

consteval std::meta::info forwarders_type(
    const std::vector<std::meta::info>& base_types) {
  return std::meta::substitute(^^forwarders, base_types);
}

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_FORWARDERS_HXX_

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
// The shape-only vtable for a bare interface: a struct of named function
// pointers, one per interface member, synthesized with
// std::meta::define_aggregate.
//
// define_aggregate can only be called on an incomplete type declared at the
// exact same scope as the consteval block that completes it: neither
// completing a class from inside its own body, nor reaching into a sibling
// class template's body from an enclosing function or class, is accepted
// (the compiler rejects both as "intervenes between 'consteval' block ...
// and ... scope"). Each vtable type here is therefore a local struct
// declared inside its own consteval function template, completed in that
// same function, and exposed as a type alias via a splice: one instantiation
// per Interface, giving each Interface its own type.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_VTABLE_LAYOUT_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_VTABLE_LAYOUT_HXX_

#include <meta>
#include <vector>

#include "protocol_reflection_detail/members.hxx"
#include "protocol_reflection_detail/naming.hxx"
#include "protocol_reflection_detail/types.hxx"

namespace xyz::reflection_detail {

// One data_member_spec per dispatchable member of `interface` (const-only
// if `const_only`), each a named function pointer of the member's vtable
// entry type, erased through `erased_pointer_type`. Named by
// vtable_slot_name, not vtable_entry_name: a vtable is an internal struct
// with one data member per exact overload, so overloads sharing an
// identifier (e.g. interface C's overloaded `compute`s) still need
// distinct entries here.
consteval std::vector<std::meta::info> define_vtable_entries(
    std::meta::info interface, std::meta::info erased_pointer_type,
    bool const_only) {
  std::vector<std::meta::info> specs;
  for (std::meta::info member : interface_member_functions(interface)) {
    if (const_only && !std::meta::is_const(member)) {
      continue;
    }
    specs.push_back(std::meta::data_member_spec(
        vtable_entry_pointer_type(member, erased_pointer_type),
        {.name = vtable_slot_name(member)}));
  }
  return specs;
}

template <typename Interface>
consteval std::meta::info make_const_view_vtable_info() {
  struct Incomplete;
  consteval {
    define_aggregate(^^Incomplete,
                     define_vtable_entries(^^Interface, ^^const void*,
                                           /*const_only=*/true));
  }
  return ^^Incomplete;
}

// The const-only vtable for Interface: one named function pointer per
// const member, each taking a `const void*` in place of the implicit
// object parameter.
template <typename Interface>
using const_view_vtable = typename[:make_const_view_vtable_info<Interface>():];

template <typename Interface>
consteval std::meta::info make_view_vtable_info() {
  struct Incomplete;
  consteval {
    std::vector<std::meta::info> specs = {std::meta::data_member_spec(
        ^^const_view_vtable<Interface>, {
                                            .name = "const_view"})};
    for (std::meta::info spec :
         define_vtable_entries(^^Interface, ^^void*, /*const_only=*/false)) {
      specs.push_back(spec);
    }
    define_aggregate(^^Incomplete, specs);
  }
  return ^^Incomplete;
}

// The full vtable for Interface: a `const_view` sub-object plus one named
// function pointer per member, including the const ones again, each taking
// a plain `void*` since a mutable view's entries all need mutable access
// even when the member itself is const.
template <typename Interface>
using view_vtable = typename[:make_view_vtable_info<Interface>():];

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_VTABLE_LAYOUT_HXX_

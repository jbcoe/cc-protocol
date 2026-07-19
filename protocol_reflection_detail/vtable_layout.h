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
// Vtable layout for the C++26 reflection backend: the shape of a vtable for
// a given interface, fixed by the interface alone before any implementation
// is chosen. Split out of protocol_reflection.h because this piece is
// self-contained (depends only on members.h, types.h, and naming.h, nothing
// from conformance-checking or the vtable-instance/dispatch machinery) and
// independently testable: vtable_layout_test.cc includes this header
// directly rather than the whole backend, checking that the generated
// aggregate has the right fields using synthetic probe structs alone.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_VTABLE_LAYOUT_H_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_VTABLE_LAYOUT_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <meta>
#include <vector>

#include "protocol_reflection_detail/members.h"
#include "protocol_reflection_detail/naming.h"
#include "protocol_reflection_detail/types.h"

namespace xyz {
namespace reflection_detail {

using std::meta::info;

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

}  // namespace reflection_detail
}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_VTABLE_LAYOUT_H_

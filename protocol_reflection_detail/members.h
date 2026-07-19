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
// Interface member enumeration for the C++26 reflection backend: deciding
// which of an interface type's members are dispatchable protocol members at
// all. Split out of protocol_reflection.h because this piece is
// self-contained (depends only on the standard library and <meta>, not on
// naming, candidate resolution, or vtable machinery) and independently
// testable: members_test.cc includes this header directly rather than the
// whole backend.
#ifndef XYZ_PROTOCOL_REFLECTION_MEMBERS_H_
#define XYZ_PROTOCOL_REFLECTION_MEMBERS_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <meta>
#include <string_view>
#include <vector>

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
// (see protocol_reflection.h's class definitions). An interface member
// function with one of these names would be silently hidden by C++
// name-hiding rules rather than reachable through the generated forwarders,
// so it is rejected with a static_assert instead of compiling to a silently
// broken call.
consteval bool has_reserved_interface_member_name(info interface_type) {
  for (info member : interface_member_functions(interface_type)) {
    if (!std::meta::has_identifier(member)) continue;
    std::string_view name = std::meta::identifier_of(member);
    if (name == "swap" || name == "valueless_after_move") return true;
  }
  return false;
}

// Rvalue-qualified interface members (declared `&&`) are rejected: this
// backend does not yet give a forwarder its own `&&`-qualified operator().
// Left unrejected, such a member would silently compile to an ordinary,
// always-callable forwarder instead of one restricted to a moved-from
// protocol, misrepresenting what the interface declared.
consteval bool has_rvalue_qualified_interface_member(info interface_type) {
  for (info member : interface_member_functions(interface_type)) {
    if (std::meta::is_rvalue_reference_qualified(member)) return true;
  }
  return false;
}

// The interface's member list, enumerated once per interface and persisted
// with define_static_array. Vtable construction (make_vtable_entry) and the
// concept checks (models_reflected_interface) index this array; forwarder
// generation (named_forwarders, make_operator_forwarders) instead
// re-enumerates via interface_member_functions directly.
template <typename Interface>
inline constexpr auto interface_members =
    std::define_static_array(interface_member_functions(^^Interface));

}  // namespace reflection_detail
}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_MEMBERS_H_

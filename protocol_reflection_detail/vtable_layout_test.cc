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

#include "protocol_reflection_detail/vtable_layout.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <type_traits>

// Direct tests of vtable layout (vtable_layout.h): the shape of a vtable for
// a given interface. This file includes only the vtable-layout header, not
// protocol.h, and no implementation or xyz::protocol is ever plugged in.
// These checks are about the generated aggregate's fields.

namespace {

struct LayoutProbe {
  int foo(int) const { return 0; }

  void bar() {}
};

consteval std::meta::info FindInterfaceMember(std::string_view name) {
  for (std::meta::info member :
       xyz::reflection_detail::interface_member_functions(^^LayoutProbe)) {
    if (std::meta::identifier_of(member) == name) return member;
  }
  return std::meta::info{};
}

consteval std::meta::info FindDataMemberNamed(std::meta::info class_type,
                                              std::string_view name) {
  for (std::meta::info member : std::meta::nonstatic_data_members_of(
           class_type, std::meta::access_context::current())) {
    if (std::meta::identifier_of(member) == name) return member;
  }
  return std::meta::info{};
}

using ViewEntries =
    xyz::reflection_detail::view_vtable<LayoutProbe>::view_entries;

consteval bool ViewEntryExistsFor(std::string_view member_name) {
  std::meta::info member = FindInterfaceMember(member_name);
  if (member == std::meta::info{}) return false;
  std::string entry_name = xyz::reflection_detail::vtable_entry_name(member);
  return FindDataMemberNamed(^^ViewEntries, entry_name) != std::meta::info{};
}

TEST(ReflectionVtableLayoutTest, ViewEntryExistsForConstMember) {
  static_assert(ViewEntryExistsFor("foo"));
}

TEST(ReflectionVtableLayoutTest, ViewEntryExistsForNonConstMember) {
  static_assert(ViewEntryExistsFor("bar"));
}

consteval std::size_t CountDataMembers(std::meta::info class_type) {
  std::size_t count = 0;
  for ([[maybe_unused]] std::meta::info member :
       std::meta::nonstatic_data_members_of(
           class_type, std::meta::access_context::current())) {
    ++count;
  }
  return count;
}

TEST(ReflectionVtableLayoutTest, ViewEntriesHasOneFieldPerInterfaceMember) {
  static_assert(
      CountDataMembers(^^ViewEntries) ==
      xyz::reflection_detail::interface_member_functions(^^LayoutProbe).size());
}

consteval bool EntryTypeMatchesExpected(std::string_view member_name) {
  std::meta::info member = FindInterfaceMember(member_name);
  std::meta::info erased_pointer_type =
      std::meta::is_const(member) ? ^^const void* : ^^void*;
  std::meta::info expected_type =
      xyz::reflection_detail::vtable_entry_pointer_type(member,
                                                        erased_pointer_type);
  std::string entry_name = xyz::reflection_detail::vtable_entry_name(member);
  std::meta::info entry = FindDataMemberNamed(^^ViewEntries, entry_name);
  return std::meta::dealias(std::meta::type_of(entry)) ==
         std::meta::dealias(expected_type);
}

TEST(ReflectionVtableLayoutTest, ConstMemberEntryUsesConstErasedPointer) {
  static_assert(EntryTypeMatchesExpected("foo"));
}

TEST(ReflectionVtableLayoutTest, NonConstMemberEntryUsesMutableErasedPointer) {
  static_assert(EntryTypeMatchesExpected("bar"));
}

using OwningVtable =
    xyz::reflection_detail::owning_vtable<LayoutProbe,
                                          std::allocator<std::byte>>::vtable;

TEST(ReflectionVtableLayoutTest, OwningVtableHasLifetimeAndViewFields) {
  static_assert(FindDataMemberNamed(^^OwningVtable, "xyz_protocol_clone") !=
                std::meta::info{});
  static_assert(FindDataMemberNamed(^^OwningVtable, "xyz_protocol_move") !=
                std::meta::info{});
  static_assert(FindDataMemberNamed(^^OwningVtable, "xyz_protocol_destroy") !=
                std::meta::info{});
  static_assert(FindDataMemberNamed(^^OwningVtable, "view_vt") !=
                std::meta::info{});
  static_assert(FindDataMemberNamed(^^OwningVtable, "entries") !=
                std::meta::info{});
}

TEST(ReflectionVtableLayoutTest, OwningVtableCarriesTagAndProtocolType) {
  static_assert(
      requires { typename OwningVtable::xyz_reflection_owning_vtable_tag; });
  static_assert(
      std::is_same_v<typename OwningVtable::protocol_type, LayoutProbe>);
}

}  // namespace

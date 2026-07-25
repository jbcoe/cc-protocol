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
#include "protocol_reflection_detail/vtable_layout.hxx"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

namespace {

using xyz::reflection_detail::const_view_vtable;
using xyz::reflection_detail::find_data_member;
using xyz::reflection_detail::view_vtable;
using xyz::reflection_detail::vtable_slot_name;

struct Fixture {
  int value() const { return 0; }

  void set_value(int) {}
};

// A template-id (e.g. const_view_vtable<Fixture>) can't be reflected
// directly with ^^ when the template is itself an alias template ("'^^'
// cannot be applied to a using-declaration"). Naming the instantiation
// through a plain, non-template alias first works around it.
using ConstViewVtableFixture = const_view_vtable<Fixture>;
using ViewVtableFixture = view_vtable<Fixture>;

// Entries are looked up by name rather than typed as literal members,
// because vtable_slot_name's output isn't a fixed, hand-writable identifier
// once an interface has overloads: it qualifies by full signature
// (naming.hxx). These tests use the same lookup real callers use in
// dispatch, rather than assuming a nice literal name.

TEST(ConstViewVtable, HasOneEntryPerConstMemberTakingAConstErasedPointer) {
  constexpr std::meta::info value_entry = find_data_member(
      ^^ConstViewVtableFixture, vtable_slot_name(^^Fixture::value));
  static_assert(
      std::is_same_v<
          decltype(std::declval<const_view_vtable<Fixture>>().[:value_entry:]),
          int (*)(const void*)>);
  static_assert(sizeof(const_view_vtable<Fixture>) == sizeof(void (*)()));
}

TEST(ViewVtable, HasAConstViewSubobjectAndOneEntryPerMember) {
  static_assert(
      std::is_same_v<decltype(std::declval<view_vtable<Fixture>>().const_view),
                     const_view_vtable<Fixture>>);

  constexpr std::meta::info value_entry =
      find_data_member(^^ViewVtableFixture, vtable_slot_name(^^Fixture::value));
  static_assert(std::is_same_v<
                decltype(std::declval<view_vtable<Fixture>>().[:value_entry:]),
                int (*)(void*)>);

  constexpr std::meta::info set_value_entry = find_data_member(
      ^^ViewVtableFixture, vtable_slot_name(^^Fixture::set_value));
  static_assert(std::is_same_v<decltype(std::declval<view_vtable<Fixture>>()
                                            .[:set_value_entry:]),
                               void (*)(void*, int)>);
}

TEST(ViewVtable, EntriesAreCallableFunctionPointers) {
  constexpr std::meta::info const_value_entry = find_data_member(
      ^^ConstViewVtableFixture, vtable_slot_name(^^Fixture::value));
  constexpr std::meta::info value_entry =
      find_data_member(^^ViewVtableFixture, vtable_slot_name(^^Fixture::value));
  constexpr std::meta::info set_value_entry = find_data_member(
      ^^ViewVtableFixture, vtable_slot_name(^^Fixture::set_value));

  view_vtable<Fixture> vtable{};
  vtable.const_view.[:const_value_entry:] = +[](const void*) { return 42; };
  vtable.[:value_entry:] = +[](void*) { return 42; };
  vtable.[:set_value_entry:] = +[](void*, int) {};

  EXPECT_EQ(vtable.const_view.[:const_value_entry:](nullptr), 42);
  EXPECT_EQ(vtable.[:value_entry:](nullptr), 42);
  vtable.[:set_value_entry:](nullptr, 1);
}

}  // namespace

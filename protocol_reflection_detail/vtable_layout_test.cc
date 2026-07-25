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
using xyz::reflection_detail::view_vtable;

struct Fixture {
  int value() const { return 0; }

  void set_value(int) {}
};

TEST(ConstViewVtable, HasOneEntryPerConstMemberTakingAConstErasedPointer) {
  static_assert(
      std::is_same_v<decltype(std::declval<const_view_vtable<Fixture>>().value),
                     int (*)(const void*)>);
  static_assert(sizeof(const_view_vtable<Fixture>) == sizeof(void (*)()));
}

TEST(ViewVtable, HasAConstViewSubobjectAndOneEntryPerMember) {
  static_assert(
      std::is_same_v<decltype(std::declval<view_vtable<Fixture>>().const_view),
                     const_view_vtable<Fixture>>);
  static_assert(
      std::is_same_v<decltype(std::declval<view_vtable<Fixture>>().value),
                     int (*)(void*)>);
  // set_value's own literal underscore is escaped too (naming.hxx escapes
  // every non-alphanumeric byte, including "_" itself): set_5fvalue.
  static_assert(
      std::is_same_v<decltype(std::declval<view_vtable<Fixture>>().set_5fvalue),
                     void (*)(void*, int)>);
}

TEST(ViewVtable, EntriesAreCallableFunctionPointers) {
  view_vtable<Fixture> vtable{
      .const_view = {.value = +[](const void*) { return 42; }},
      .value = +[](void*) { return 42; },
      .set_5fvalue = +[](void*, int) {}};

  EXPECT_EQ(vtable.const_view.value(nullptr), 42);
  EXPECT_EQ(vtable.value(nullptr), 42);
  vtable.set_5fvalue(nullptr, 1);
}

}  // namespace

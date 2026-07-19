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

#include "naming.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

// Direct tests of reflection_detail's naming helpers (naming.h), which every
// vtable slot's identifier depends on. This file includes only the naming
// header, not protocol.h, and none of the structs below are ever wrapped in
// xyz::protocol: a collision or instability here is caught at its source
// instead of surfacing as a confusing dispatch or narrowing failure
// somewhere else.
//
// These helpers are consteval, so their properties are checked with
// static_assert rather than EXPECT_EQ/EXPECT_NE: the result is a compile-time
// fact, and comparing within one constant expression avoids the (unrelated)
// restriction on a heap-allocating std::string escaping constant evaluation
// to become an ordinary runtime value. Each check still sits inside a named
// TEST() so it shows up as its own passing test once the static_assert has
// compiled.

namespace {

TEST(ReflectionDetailTest, IdentifierSafeStringEscapesLiteralUnderscore) {
  // Escaping a literal underscore, rather than leaving it alone, is what
  // prevents this exact collision: under a scheme that left '_' unescaped,
  // "a_20" (literal 'a', '_', '2', '0') and "a " (a space, itself escaped
  // to "_20") would both encode to "a_20".
  static_assert(xyz::reflection_detail::identifier_safe_string("a_20") !=
                xyz::reflection_detail::identifier_safe_string("a "));
}

TEST(ReflectionDetailTest, IdentifierSafeStringPassesAlnumThrough) {
  static_assert(xyz::reflection_detail::identifier_safe_string("Abc123") ==
                "Abc123");
}

TEST(ReflectionDetailTest, IdentifierSafeStringEscapesNonAlnumAsHex) {
  static_assert(xyz::reflection_detail::identifier_safe_string(" ") == "_20");
  static_assert(xyz::reflection_detail::identifier_safe_string("_") == "_5f");
  static_assert(xyz::reflection_detail::identifier_safe_string("(") == "_28");
}

consteval bool AllOperatorSpellingsAreUnique() {
  std::vector<std::string_view> spellings;
  template for (constexpr std::meta::info e : std::define_static_array(
                    std::meta::enumerators_of(^^std::meta::operators))) {
    std::string_view spelling =
        xyz::reflection_detail::operator_spelling([:e:]);
    for (std::string_view existing : spellings) {
      if (existing == spelling) return false;
    }
    spellings.push_back(spelling);
  }
  return true;
}

TEST(ReflectionDetailTest, OperatorSpellingIsUniquePerOperator) {
  // operator_spelling feeds directly into every operator's mangled vtable
  // entry name; two operator kinds sharing a spelling would collide on one
  // vtable slot.
  static_assert(AllOperatorSpellingsAreUnique());
}

struct ConstOverloadNamingProbe {
  void call(int) {}

  void call(int) const {}
};

consteval std::string EntryNameForConstness(bool want_const) {
  for (std::meta::info member : std::meta::members_of(
           ^^ConstOverloadNamingProbe, std::meta::access_context::current())) {
    if (!std::meta::has_identifier(member)) continue;
    if (std::meta::identifier_of(member) != "call") continue;
    if (std::meta::is_const(member) != want_const) continue;
    return xyz::reflection_detail::vtable_entry_name(member);
  }
  return "";
}

TEST(ReflectionDetailTest, VtableEntryNameDistinguishesConstness) {
  // call(int) and call(int) const share everything vtable_entry_name
  // encodes except constness; if constness weren't part of the entry name,
  // protocol would generate one colliding vtable slot for what should be
  // two independently dispatchable overloads.
  static_assert(EntryNameForConstness(false) != EntryNameForConstness(true));
}

TEST(ReflectionDetailTest, VtableEntryNameIsStableAcrossCalls) {
  // Narrowing conversions (copy_vtable_entries) match entries between two
  // different interfaces by name, so reflecting the same member twice must
  // always produce the identical name.
  static_assert(EntryNameForConstness(false) == EntryNameForConstness(false));
}

struct ParameterCountNamingProbe {
  void call(int) {}

  void call(int, int) {}
};

consteval std::string EntryNameForParameterCount(std::size_t param_count) {
  for (std::meta::info member : std::meta::members_of(
           ^^ParameterCountNamingProbe, std::meta::access_context::current())) {
    if (!std::meta::has_identifier(member)) continue;
    if (std::meta::identifier_of(member) != "call") continue;
    std::size_t count = 0;
    for ([[maybe_unused]] std::meta::info parameter :
         std::meta::parameters_of(member)) {
      ++count;
    }
    if (count != param_count) continue;
    return xyz::reflection_detail::vtable_entry_name(member);
  }
  return "";
}

TEST(ReflectionDetailTest, VtableEntryNameDistinguishesParameterCount) {
  static_assert(EntryNameForParameterCount(1) != EntryNameForParameterCount(2));
}

}  // namespace

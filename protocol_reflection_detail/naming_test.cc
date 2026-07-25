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
#include "protocol_reflection_detail/naming.hxx"

#include <gtest/gtest.h>

#include <meta>
#include <string>
#include <string_view>

namespace {

using xyz::reflection_detail::identifier_safe_string;
using xyz::reflection_detail::vtable_entry_name;
using xyz::reflection_detail::vtable_slot_name;

TEST(IdentifierSafeString, AlphanumericPassesThroughUnchanged) {
  EXPECT_EQ(identifier_safe_string("abcXYZ123"), "abcXYZ123");
}

TEST(IdentifierSafeString, EmptyStringStaysEmpty) {
  EXPECT_EQ(identifier_safe_string(""), "");
}

TEST(IdentifierSafeString, NonAlphanumericByteBecomesUnderscoreHexPair) {
  // '(' is 0x28.
  EXPECT_EQ(identifier_safe_string("a(b"), "a_28b");
}

TEST(IdentifierSafeString, LiteralUnderscoreIsEscapedToo) {
  // '_' is 0x5f: the escape marker itself is never left unescaped, so a
  // genuine underscore in the input can never be confused with one half of
  // an escape sequence.
  EXPECT_EQ(identifier_safe_string("a_b"), "a_5fb");
}

TEST(IdentifierSafeString, DistinctInputsStayDistinctEvenWhenNaivelyAmbiguous) {
  // A scheme that replaced every non-identifier character with a single
  // "_" would map both of these to "a_b", losing the distinction. This
  // scheme keeps every input distinguishable.
  EXPECT_NE(identifier_safe_string("a(b"), identifier_safe_string("a)b"));
  EXPECT_NE(identifier_safe_string("a(b"), identifier_safe_string("a_b"));
}

TEST(IdentifierSafeString, ResultOnlyContainsValidIdentifierCharacters) {
  std::string result = identifier_safe_string("write(int, double)::operator+");
  for (char c : result) {
    EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_')
        << "unexpected character '" << c << "' in " << result;
  }
}

struct Fixture {
  int value() const { return 0; }

  void set_value(int) {}
};

TEST(VtableEntryName, NamesAnOrdinaryMemberByItsExactIdentifier) {
  // Unlike vtable_slot_name's internal, escaped names below.
  constexpr const char* value_name =
      std::define_static_string(vtable_entry_name(^^Fixture::value));
  constexpr const char* set_value_name =
      std::define_static_string(vtable_entry_name(^^Fixture::set_value));

  EXPECT_STREQ(value_name, "value");
  EXPECT_STREQ(set_value_name, "set_value");
}

struct Overloaded {
  int compute(int x) { return x; }

  double compute(double x) const { return x; }
};

consteval std::meta::info compute_overload(bool take_double) {
  for (std::meta::info member : std::meta::members_of(
           ^^Overloaded, std::meta::access_context::current())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == "compute" &&
        std::meta::is_const(member) == take_double) {
      return member;
    }
  }
  throw std::meta::exception("overload not found", ^^void);
}

TEST(VtableSlotName, DistinctOverloadsGetDistinctNames) {
  constexpr const char* int_overload =
      std::define_static_string(vtable_slot_name(compute_overload(false)));
  constexpr const char* double_overload =
      std::define_static_string(vtable_slot_name(compute_overload(true)));

  EXPECT_STRNE(int_overload, double_overload);
}

TEST(VtableSlotName, SameOverloadGivesTheSameNameEveryTime) {
  constexpr const char* first =
      std::define_static_string(vtable_slot_name(compute_overload(false)));
  constexpr const char* second =
      std::define_static_string(vtable_slot_name(compute_overload(false)));

  EXPECT_STREQ(first, second);
}

}  // namespace

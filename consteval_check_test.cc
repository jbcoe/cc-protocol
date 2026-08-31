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

#include "consteval_check.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using xyz::consteval_check_failure;

namespace {

// ---------------------------------------------------------------------------
// Compile-time usage.
// ---------------------------------------------------------------------------

TEST(ConstevalCheckTest, PassingChecksInConstevalBlock) {
  // Every comparison operator, an arithmetic left-hand side, a string_view
  // comparison, and a unary bool check, all evaluated during constant
  // evaluation. Compiling this test is the assertion: a failing check would
  // throw during the `consteval` block and fail to compile.
  consteval {
    int a = 5;
    int b = 15;
    std::string_view name = "hello";
    bool ready = true;

    XYZ_CONSTEVAL_CHECK(a + b == 20);
    XYZ_CONSTEVAL_CHECK(a != b);
    XYZ_CONSTEVAL_CHECK(a < b);
    XYZ_CONSTEVAL_CHECK(b > a);
    XYZ_CONSTEVAL_CHECK(a <= 5);
    XYZ_CONSTEVAL_CHECK(b >= 15);
    XYZ_CONSTEVAL_CHECK(name == "hello");
    XYZ_CONSTEVAL_CHECK(ready);
  }
}

// Catching the failure at constant evaluation time requires P3068 constexpr
// exceptions, which GCC trunk implements but the clang-p2996 fork used for
// clang-tidy does not. The run-time tests below cover catchability there.
#ifdef __cpp_constexpr_exceptions
consteval bool ConstevalCheckFailureIsCatchable() {
  try {
    XYZ_CONSTEVAL_CHECK(10 < 5);
    return false;
  } catch (const consteval_check_failure&) {
    return true;
  }
}

TEST(ConstevalCheckTest, FailureIsCatchableAtCompileTime) {
  static_assert(ConstevalCheckFailureIsCatchable());
}
#endif  // __cpp_constexpr_exceptions

// ---------------------------------------------------------------------------
// Run-time usage: `XYZ_CONSTEVAL_CHECK` is plain constexpr code, so it also
// works at run time, which lets these tests inspect the thrown message.
// ---------------------------------------------------------------------------

TEST(ConstevalCheckTest, MessageContainsLocationExpressionAndExpansion) {
  {
    int value = 5;
    try {
      XYZ_CONSTEVAL_CHECK(value + 10 == 21);
      FAIL() << "expected consteval_check_failure to be thrown";
    } catch (const consteval_check_failure& failure) {
      std::string_view what = failure.what();
      EXPECT_NE(what.find("consteval_check_test.cc:"), std::string_view::npos);
      EXPECT_NE(what.find("check failed: value + 10 == 21"),
                std::string_view::npos);
      EXPECT_NE(what.find("[15 == 21]"), std::string_view::npos);
    }
  }

  {
    std::string_view actual = "y";
    try {
      XYZ_CONSTEVAL_CHECK(actual == "x");
      FAIL() << "expected consteval_check_failure to be thrown";
    } catch (const consteval_check_failure& failure) {
      std::string_view what = failure.what();
      EXPECT_NE(what.find("[\"y\" == \"x\"]"), std::string_view::npos);
    }
  }

  {
    bool left = true;
    bool right = false;
    try {
      XYZ_CONSTEVAL_CHECK(left == right);
      FAIL() << "expected consteval_check_failure to be thrown";
    } catch (const consteval_check_failure& failure) {
      std::string_view what = failure.what();
      EXPECT_NE(what.find("[true == false]"), std::string_view::npos);
    }
  }

  {
    bool ready = false;
    try {
      XYZ_CONSTEVAL_CHECK(ready);
      FAIL() << "expected consteval_check_failure to be thrown";
    } catch (const consteval_check_failure& failure) {
      std::string_view what = failure.what();
      EXPECT_EQ(what.find('['), std::string_view::npos);
    }
  }
}

// A type with no display support: not bool, not integral, not convertible to
// std::string_view, and (outside a reflection context) not std::meta::info.
struct Opaque {
  int tag;

  constexpr bool operator==(const Opaque& other) const {
    return tag == other.tag;
  }
};

TEST(ConstevalCheckTest, UnsupportedTypeDisplaysPlaceholder) {
  Opaque left{.tag = 1};
  Opaque right{.tag = 2};
  try {
    XYZ_CONSTEVAL_CHECK(left == right);
    FAIL() << "expected consteval_check_failure to be thrown";
  } catch (const consteval_check_failure& failure) {
    std::string_view what = failure.what();
    EXPECT_NE(what.find("{?}"), std::string_view::npos);
  }
}

}  // namespace

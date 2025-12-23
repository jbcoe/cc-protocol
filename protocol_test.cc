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

#include "protocol.h"

#include <gtest/gtest.h>

#include <string_view>
#include <utility>

#include "post_instantiation_example.h"

namespace {

class ALike {
  int x_ = 42;
  std::string name_ = "ALike";

 public:
  ALike() = default;
  ALike(int x) : x_(x) {};
  ALike(std::string_view name) : name_(name) {};
  ALike(int x, std::string_view name) : x_(x), name_(name) {};

  std::string_view name() const { return name_; }

  int count() { return x_; }
};

TEST(ProtocolTest, InPlaceCtorNoArgs) {
  xyz::protocol_A<> a(std::in_place_type<ALike>);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(ProtocolTest, InPlaceCtorSingleArg) {
  xyz::protocol_A<> a(std::in_place_type<ALike>, 42);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(ProtocolTest, InPlaceCtorMultipleArgs) {
  xyz::protocol_A<> a(std::in_place_type<ALike>, 180, "CustomName");
  EXPECT_EQ(a.name(), "CustomName");
  EXPECT_EQ(a.count(), 180);
}

TEST(ProtocolTest, MemberFunctions) {
  xyz::protocol_A<> a(std::in_place_type<ALike>);
  EXPECT_EQ(a.name(), "ALike");
  EXPECT_EQ(a.count(), 42);
}

TEST(ProtocolTest, CopyCtor) {
  xyz::protocol_A<> a(std::in_place_type<ALike>, 100, "Original");
  xyz::protocol_A<> aa(a);
  EXPECT_EQ(aa.name(), "Original");
  EXPECT_EQ(aa.count(), 100);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(ProtocolTest, MoveCtor) {
  xyz::protocol_A<> a(std::in_place_type<ALike>, 100, "Original");
  xyz::protocol_A<> aa(std::move(a));
  EXPECT_EQ(aa.name(), "Original");
  EXPECT_EQ(aa.count(), 100);
  EXPECT_TRUE(
      a.valueless_after_move());  // NOLINT(clang-analyzer-cplusplus.Move)
}
}  // namespace

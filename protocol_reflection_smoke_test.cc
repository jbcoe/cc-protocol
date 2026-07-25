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
// Exercises real xyz::protocol dispatch on a throwaway single-const-method
// interface, keeping this file's compile/link surface minimal.
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>

#include "protocol.h"

namespace {

struct Greeter {
  std::string_view name() const;
};

struct GreeterImpl {
  std::string value;

  std::string_view name() const { return value; }
};

struct NotAGreeter {
  int compute() const;
};

TEST(ProtocolReflectionSmoke, DispatchesToTheRealImplementation) {
  xyz::protocol<Greeter> p(std::in_place_type<GreeterImpl>, "hello");
  EXPECT_EQ(p.name(), "hello");
}

TEST(ProtocolReflectionSmoke, CopyIsIndependentAndDispatchesTheSame) {
  xyz::protocol<Greeter> p(std::in_place_type<GreeterImpl>, "hello");
  xyz::protocol<Greeter> copy(p);
  EXPECT_EQ(copy.name(), "hello");
}

TEST(ProtocolReflectionSmoke, MoveLeavesTheSourceValuelessAndTargetWorking) {
  xyz::protocol<Greeter> p(std::in_place_type<GreeterImpl>, "hello");
  xyz::protocol<Greeter> moved(std::move(p));
  EXPECT_EQ(moved.name(), "hello");
  EXPECT_TRUE(p.valueless_after_move());
}

TEST(ProtocolReflectionSmoke, NonConformingTypeFailsToCompile) {
  static_assert(!std::is_constructible_v<xyz::protocol<Greeter>,
                                         std::in_place_type_t<NotAGreeter>>);
}

}  // namespace

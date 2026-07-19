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

#include "protocol_reflection_detail/conformance.h"

#include <gtest/gtest.h>

// Direct tests of the reflection backend's conformance concepts
// (conformance.h): does an implementation type satisfy an interface,
// member by member. This file includes only the conformance header, not
// protocol.h, and no xyz::protocol or real vtable is ever involved.

namespace {

struct SimpleInterface {
  int compute(int) const;
};

struct GoodImplementation {
  int compute(int x) const { return x; }
};

TEST(ReflectionConformanceTest, SatisfiesWhenMemberMatches) {
  static_assert(xyz::reflection_protocol_const_concept<GoodImplementation,
                                                       SimpleInterface>);
  static_assert(
      xyz::reflection_protocol_concept<GoodImplementation, SimpleInterface>);
}

struct MissingMethodImplementation {
  int other() const { return 0; }
};

TEST(ReflectionConformanceTest, FailsWhenMethodMissing) {
  static_assert(
      !xyz::reflection_protocol_const_concept<MissingMethodImplementation,
                                              SimpleInterface>);
}

struct NotConvertible {};

struct WrongReturnTypeImplementation {
  NotConvertible compute(int) const { return {}; }
};

TEST(ReflectionConformanceTest, FailsWhenReturnTypeNotConvertible) {
  static_assert(
      !xyz::reflection_protocol_const_concept<WrongReturnTypeImplementation,
                                              SimpleInterface>);
}

struct NonConstInterface {
  int compute(int);
};

struct ConstOnlyImplementation {
  int compute(int) const { return 0; }
};

TEST(ReflectionConformanceTest, ConstCandidateSatisfiesNonConstMember) {
  // A const-qualified implementation candidate can serve any interface
  // member regardless of the member's own constness: calling a const
  // method through a non-const access path is always fine.
  static_assert(xyz::reflection_protocol_concept<ConstOnlyImplementation,
                                                 NonConstInterface>);
}

struct ConstInterface {
  int compute(int) const;
};

struct MutatingOnlyImplementation {
  int compute(int) { return 0; }
};

TEST(ReflectionConformanceTest, MutatingCandidateFailsConstMember) {
  // compute() const on the interface can only be satisfied by a const
  // candidate, regardless of which concept is checked: a const interface
  // member is unreachable through a non-const self either way.
  static_assert(
      !xyz::reflection_protocol_const_concept<MutatingOnlyImplementation,
                                              ConstInterface>);
  static_assert(!xyz::reflection_protocol_concept<MutatingOnlyImplementation,
                                                  ConstInterface>);
}

}  // namespace

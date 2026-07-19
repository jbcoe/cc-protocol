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

#include "thunk.h"

#include <gtest/gtest.h>

#include <utility>

// Direct tests of erased_call_thunk (thunk.h), using hand-rolled fake
// "candidate" types rather than the real candidate_overload_set machinery.
// No xyz::protocol is involved: the thunk's own contract only requires a
// reflected type constructible from a pointer and callable with the given
// parameters.

namespace {

struct FakeImplementation {
  int value = 0;
};

struct FakeCandidate {
  FakeImplementation* self;

  explicit FakeCandidate(FakeImplementation* self) : self(self) {}

  int operator()(int x) { return self->value + x; }
};

TEST(ReflectionThunkTest, ForwardsCallToMergedCandidate) {
  FakeImplementation impl{10};
  void* erased = &impl;
  int result = xyz::reflection_detail::erased_call_thunk<
      FakeImplementation, ^^FakeCandidate, int(int), false, false>::call(erased,
                                                                         5);
  EXPECT_EQ(result, 15);
}

struct FakeConstCandidate {
  const FakeImplementation* self;

  explicit FakeConstCandidate(const FakeImplementation* self) : self(self) {}

  int operator()(int x) const { return self->value + x; }
};

TEST(ReflectionThunkTest, ConstErasedUsesConstPointer) {
  const FakeImplementation impl{20};
  const void* erased = &impl;
  int result =
      xyz::reflection_detail::erased_call_thunk<FakeImplementation,
                                                ^^FakeConstCandidate, int(int),
                                                true, false>::call(erased, 5);
  EXPECT_EQ(result, 25);
}

struct VoidCandidate {
  FakeImplementation* self;

  explicit VoidCandidate(FakeImplementation* self) : self(self) {}

  void operator()(int x) { self->value = x; }
};

TEST(ReflectionThunkTest, HandlesVoidReturnType) {
  FakeImplementation impl{0};
  void* erased = &impl;
  xyz::reflection_detail::erased_call_thunk<FakeImplementation, ^^VoidCandidate,
                                            void(int), false,
                                            false>::call(erased, 42);
  EXPECT_EQ(impl.value, 42);
}

struct MultiParamCandidate {
  FakeImplementation* self;

  explicit MultiParamCandidate(FakeImplementation* self) : self(self) {}

  int operator()(int a, int b) { return self->value + a + b; }
};

TEST(ReflectionThunkTest, ForwardsMultipleParametersInOrder) {
  FakeImplementation impl{1};
  void* erased = &impl;
  int result = xyz::reflection_detail::erased_call_thunk<
      FakeImplementation, ^^MultiParamCandidate, int(int, int), false,
      false>::call(erased, 2, 3);
  EXPECT_EQ(result, 6);
}

TEST(ReflectionThunkTest, IsNoexceptExactlyWhenRequested) {
  using NoexceptThunk = xyz::reflection_detail::erased_call_thunk<
      FakeImplementation, ^^FakeCandidate, int(int), false, true>;
  static_assert(noexcept(NoexceptThunk::call(std::declval<void*>(), 0)));

  using ThrowingThunk = xyz::reflection_detail::erased_call_thunk<
      FakeImplementation, ^^FakeCandidate, int(int), false, false>;
  static_assert(!noexcept(ThrowingThunk::call(std::declval<void*>(), 0)));
}

}  // namespace

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
#include "protocol_reflection_detail/thunk.hxx"

#include <gtest/gtest.h>

namespace {

using xyz::reflection_detail::erased_call_thunk;

struct Implementation {
  int value = 0;

  int get() const { return value; }

  void set(int v) { value = v; }
};

// Hand-rolled fake candidates: each is just a default-constructible callable
// taking the implementation reference plus the call's own arguments.
struct GetCandidate {
  int operator()(const Implementation& impl) const { return impl.get(); }
};

struct SetCandidate {
  void operator()(Implementation& impl, int v) const { impl.set(v); }
};

TEST(ErasedCallThunk, ConstErasedCallRoundTripsTheReturnValue) {
  Implementation impl{.value = 42};
  using Thunk =
      erased_call_thunk<Implementation, GetCandidate, int(), true, false>;
  EXPECT_EQ(Thunk::call(&impl), 42);
}

TEST(ErasedCallThunk, NonConstErasedCallForwardsArgumentsAndMutates) {
  Implementation impl;
  using Thunk =
      erased_call_thunk<Implementation, SetCandidate, void(int), false, false>;
  Thunk::call(&impl, 7);
  EXPECT_EQ(impl.value, 7);
}

TEST(ErasedCallThunk,
     IsNoexceptTemplateParameterControlsTheThunksNoexceptness) {
  using NoexceptThunk =
      erased_call_thunk<Implementation, GetCandidate, int(), true, true>;
  using ThrowingThunk =
      erased_call_thunk<Implementation, GetCandidate, int(), true, false>;
  static_assert(noexcept(NoexceptThunk::call(nullptr)));
  static_assert(!noexcept(ThrowingThunk::call(nullptr)));
}

}  // namespace

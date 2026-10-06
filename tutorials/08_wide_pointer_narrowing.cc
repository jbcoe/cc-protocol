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

#include <gtest/gtest.h>

#include <cstddef>

namespace xyz::tutorials::wide_pointer_narrowing {

// The tests count vtable calls.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local std::size_t vtable_calls = 0;

struct FooVtable {
  int (*fn_foo)(void*);
};

struct BarVtable {
  int (*fn_bar)(void*);
};

// Defining the wide vtable in terms of Foo and Bar vtables allows
// narrowing conversions without performing nested vtable calls.
struct WideVtable : FooVtable, BarVtable {};

struct WidePtr {
  void* ptr_;
  const WideVtable* vtable_;

  template <typename T>
  explicit WidePtr(T* t) : ptr_(t) {
    static constexpr WideVtable vtable{
        {+[] /* foo */ (void* t) {
          ++vtable_calls;
          return static_cast<T*>(t)->foo();
        }},
        {+[] /* bar */ (void* t) {
          ++vtable_calls;
          return static_cast<T*>(t)->bar();
        }},
    };
    vtable_ = &vtable;
  }

  int foo() const { return vtable_->fn_foo(ptr_); }

  int bar() const { return vtable_->fn_bar(ptr_); }
};

struct FooPtr {
  void* ptr_;
  const FooVtable* vtable_;

  template <typename T>
  explicit FooPtr(T* t) : ptr_(t) {
    static constexpr FooVtable vtable{
        +[](void* t) {
          ++vtable_calls;
          return static_cast<T*>(t)->foo();
        },
    };
    vtable_ = &vtable;
  }

  explicit FooPtr(WidePtr wptr) : ptr_(wptr.ptr_), vtable_(wptr.vtable_) {}

  int foo() const { return vtable_->fn_foo(ptr_); }
};

struct BarPtr {
  void* ptr_;
  const BarVtable* vtable_;

  template <typename T>
  explicit BarPtr(T* t) : ptr_(t) {
    static constexpr BarVtable vtable{
        +[](void* t) {
          ++vtable_calls;
          return static_cast<T*>(t)->bar();
        },
    };
    vtable_ = &vtable;
  }

  explicit BarPtr(WidePtr wptr) : ptr_(wptr.ptr_), vtable_(wptr.vtable_) {}

  int bar() const { return vtable_->fn_bar(ptr_); }
};

struct FooBar {
  int foo() { return 0; }

  int bar() { return 1; }
};

TEST(TutorialsWidePointerNarrowing, WidePointerCheck) {
  vtable_calls = 0;

  FooBar fb;
  WidePtr wptr(&fb);

  EXPECT_EQ(wptr.foo(), 0);
  EXPECT_EQ(wptr.bar(), 1);
  EXPECT_EQ(vtable_calls, 2);
}

TEST(TutorialsWidePointerNarrowing, FooPointerCheck) {
  vtable_calls = 0;

  FooBar fb;
  FooPtr foo_ptr(&fb);
  EXPECT_EQ(foo_ptr.foo(), 0);
  EXPECT_EQ(vtable_calls, 1);
}

TEST(TutorialsWidePointerNarrowing, BarPointerCheck) {
  vtable_calls = 0;

  FooBar fb;

  BarPtr bar_ptr(&fb);
  EXPECT_EQ(bar_ptr.bar(), 1);
  EXPECT_EQ(vtable_calls, 1);
}

TEST(TutorialsWidePointerNarrowing, FooPointerNarrowingCheck) {
  vtable_calls = 0;

  FooBar fb;
  WidePtr wptr(&fb);

  FooPtr foo_ptr(wptr);
  EXPECT_EQ(foo_ptr.foo(), 0);
  EXPECT_EQ(vtable_calls, 1);
}

TEST(TutorialsWidePointerNarrowing, BarPointerNarrowingCheck) {
  vtable_calls = 0;

  FooBar fb;
  WidePtr wptr(&fb);

  BarPtr bar_ptr(wptr);
  EXPECT_EQ(bar_ptr.bar(), 1);
  EXPECT_EQ(vtable_calls, 1);
}

}  // namespace xyz::tutorials::wide_pointer_narrowing

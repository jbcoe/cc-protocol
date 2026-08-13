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

#include <utility>

// An exploration of adding allocator support to an owning type.

// A basic owning type, without allocator support.

namespace xyz::tutorials::owning_type {

  // Owner is a class that simply holds an object of type T. It
  // does not support anything besides the rule-of-five member functions
  // and swap. This class is for demonstration purposes only.
  template <typename T>
  class Owner {
    // The managed resource. We leave it as null if Owner is default
    // constructed or moved-from.
    T* obj_ = nullptr;

    void destroy() {
      delete obj_;
    }

    static T* copy(T* obj) {
      if (obj == nullptr) {
        return nullptr;
      }
      return new T(*obj);
    }
   public:
    Owner() = default;
    explicit Owner(const T& obj) : obj_(new T(obj)) { }
    explicit Owner(T&& obj) : obj_(new T(std::move(obj))) { }

    // Rule-of-five implementation below.

    ~Owner() { destroy(); }

    Owner(const Owner& other) 
      : obj_(copy(other.obj_))
    { }

    Owner& operator=(const Owner& other) {
      if (this != &other) {
        destroy();
        obj_ = copy(other.obj_);
      }
      return *this;
    }

    Owner(Owner&& other) noexcept
      : obj_(std::exchange(other.obj_, nullptr))
    { }

    Owner& operator=(Owner&& other) noexcept {
      if (this != &other) {
        destroy();
        obj_ = std::exchange(other.obj_, nullptr);
      }
      return *this;
    }

    // A swap member function to prevent redundant moves
    // when swapping.
    void swap(Owner& other) noexcept {
      std::swap(obj_, other.obj_);
    }

    bool has_value() const {
      return obj_ != nullptr;
    }

    T& get() {
      assert(has_value());
      return *obj_;
    }

    const T& get() const {
      assert(has_value());
      return *obj_;
    }
  };

  TEST(TutorialsAllocators, OwningType) {
    Owner<int> o1;
    EXPECT_FALSE(o1.has_value());
    
    Owner o2{10}; // CTAD deduces this is Owner<int>.
    EXPECT_TRUE(o2.has_value());
    EXPECT_EQ(o2.get(), 10);

    Owner o3{std::move(o2)};
    EXPECT_FALSE(o2.has_value());
    EXPECT_EQ(o3.get(), 10);

    o3 = o1;
    EXPECT_FALSE(o3.has_value());

    // All of the memory gets cleaned up at the end of scope.
  }
}
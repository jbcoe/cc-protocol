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

#include <concepts>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// A follow-up to the tutorial on polymorphism. We will expand our
// implementation of our type-erased AnimalPtr to an owning Animal type with
// value semantics.

namespace xyz::tutorials::owning_type_erasure {

// For this tutorial, we will remove the identity() function from Cat and Dog to
// focus more directly on the addition of ownership semantics.
class Cat {
 public:
  std::string_view noise() const { return "Meow"; }
};

class Dog {
 public:
  std::string_view noise() const { return "Woof"; }
};

struct vtable {
  std::string_view (*noise_func_)(const void* data);

  // NEW: destroy_ and copy_ manage Animal's lifecycle.
  void (*destroy_)(void* data);
  void* (*copy_)(const void* data);
};

// For clarity, we move the vtable definition next to the vtable itself,
// instead of as a local static variable.
template <typename T>
constexpr inline vtable vtable_for = {
    // Same as before.
    .noise_func_ = +[](const void* data) -> std::string_view {
      return static_cast<const T*>(data)->noise();
    },
    // NEW: destroys whatever type we originally had.
    .destroy_ = +[](void* data) -> void { delete static_cast<T*>(data); },
    // NEW: allocates a copy of the erased object.
    .copy_ = +[](const void* src) -> void* {
      return new T(*static_cast<const T*>(src));
    }};

// null_vtable is a no-op vtable for Animal's empty state, so Animal's
// methods can dispatch through vtable_ unconditionally instead of
// null-checking it.
constexpr inline vtable null_vtable = {
    .noise_func_ = +[](const void* data) -> std::string_view {
      throw std::bad_function_call{};
    },
    .destroy_ = +[](void* data) -> void {},
    .copy_ = +[](const void* src) -> void* { return nullptr; }};

class Animal {
  void* data_;
  const vtable* vtable_;

 public:
  // TNorm is the decayed version of T. If T is int&&, then TNorm will be int.
  // The requires clause prevents copy and move construction from mistakenly
  // selecting this constructor. Unlike AnimalPtr's constructor in tutorial
  // 01, this one takes a forwarding reference and allocates a new TNorm,
  // move-constructing it from t when t is an rvalue and copy-constructing it
  // otherwise.
  template <typename T, typename TNorm = std::decay_t<T>>
    requires(!std::same_as<TNorm, Animal>)
  Animal(T&& t)
      : data_(new TNorm(std::forward<T>(t))), vtable_(&vtable_for<TNorm>) {}

  // Copy constructor. Safe if other is moved-from, since null_vtable's copy_
  // returns nullptr and other.vtable_ is null_vtable in that state.
  Animal(const Animal& other)
      : data_(other.vtable_->copy_(other.data_)), vtable_(other.vtable_) {}

  // Move constructor: a pointer swap. The moved-from other is reset to the
  // null_vtable state.
  Animal(Animal&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        vtable_(std::exchange(other.vtable_, &null_vtable)) {}

  // Copy assignment.
  Animal& operator=(const Animal& other) {
    if (this != &other) {
      // Allocate new data first in case copy construction throws.
      void* new_data = other.vtable_->copy_(other.data_);

      vtable_->destroy_(data_);

      data_ = new_data;
      vtable_ = other.vtable_;
    }
    return *this;
  }

  // Move assignment.
  Animal& operator=(Animal&& other) noexcept {
    if (this != &other) {
      vtable_->destroy_(data_);

      data_ = std::exchange(other.data_, nullptr);
      vtable_ = std::exchange(other.vtable_, &null_vtable);
    }
    return *this;
  }

  // Destructor.
  ~Animal() { vtable_->destroy_(data_); }

  // Throws std::bad_function_call if the object is in the moved-from
  // (null_vtable) state.
  std::string_view noise() const { return vtable_->noise_func_(data_); }
};

TEST(TutorialsTypeErasure, BasicUsage) {
  std::vector<Animal> animals;
  animals.reserve(2);
  // Unlike our previous examples, this vector now owns Cat and Dog.
  // It will deallocate them when the scope ends.
  animals.emplace_back(Cat{});
  animals.emplace_back(Dog{});

  EXPECT_EQ(animals[0].noise(), "Meow");
  EXPECT_EQ(animals[1].noise(), "Woof");
}

TEST(TutorialsTypeErasure, RuleOfFive) {
  Animal a1{Cat{}};
  EXPECT_EQ(a1.noise(), "Meow");

  Animal a2{Dog{}};
  EXPECT_EQ(a2.noise(), "Woof");

  // Copy construction.
  Animal a3{a1};
  EXPECT_EQ(a3.noise(), "Meow");

  // Move construction.
  Animal a4{std::move(a2)};
  EXPECT_EQ(a4.noise(), "Woof");
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // tutorial demonstrates the moved-from state on purpose.
  EXPECT_THROW(a2.noise(), std::bad_function_call);
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)

  // Copy assignment.
  a2 = a1;
  EXPECT_EQ(a2.noise(), "Meow");

  // Move assignment.
  a3 = std::move(a4);
  EXPECT_EQ(a3.noise(), "Woof");
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // tutorial demonstrates the moved-from state on purpose.
  EXPECT_THROW(a4.noise(), std::bad_function_call);
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

}  // namespace xyz::tutorials::owning_type_erasure

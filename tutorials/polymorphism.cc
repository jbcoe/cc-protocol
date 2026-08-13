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

#include <string_view>
#include <vector>

// An intentionally brief exploration of polymorphism in C++.

// Polymorphism with templates (compile-time polymorphism).

// Cat and Dog have no common type but we can use compile-time polymorphism
// (templates) to write a function template to talk to both types.
namespace xyz::tutorials::compile_time_polymorphism {

class Cat {
 public:
  std::string_view noise() const { return "Meow"; }
};

class Dog {
 public:
  std::string_view noise() const { return "Woof"; }
};

template <typename Animal>
std::string_view make_noise(const Animal& animal) {
  return animal.noise();
}

TEST(TutorialsPolymorphism, PolymorphismWithTemplates) {
  Cat cat;
  Dog dog;

  // Cat and Dog have no common type so we can't store them directly in a
  // container. The function template can be used on either instance.

  EXPECT_EQ(make_noise(cat), "Meow");
  EXPECT_EQ(make_noise(dog), "Woof");
}

}  // namespace xyz::tutorials::compile_time_polymorphism

// Polymorphism with inheritance

// Cat and Dog have a common type. We can use a single function for both types.
// This form of inheritance is intrusive. Cat and Dog must declare Animal as a
// base class and Animal must declare which member functions can be used
// polymorphically.
namespace xyz::tutorials::polymorphism_with_inheritance {

class Animal {
 public:
  virtual std::string_view noise() const = 0;

  std::string_view identity() const { return "Animal"; }  // non-virtual
};

class Cat : public Animal {
 public:
  std::string_view noise() const { return "Meow"; }

  std::string_view identity() const { return "Cat"; }  // non-virtual
};

class Dog : public Animal {
 public:
  std::string_view noise() const { return "Woof"; }

  std::string_view identity() const { return "Dog"; }  // non-virtual
};

std::string_view make_noise(const Animal& animal) { return animal.noise(); }

TEST(TutorialsPolymorphism, PolymorphismWithInheritance) {
  Cat cat;
  Dog dog;

  // We can store pointers to Cat and Dog as pointers to Animal.
  std::vector<Animal*> animals;
  animals.reserve(2);
  animals.push_back(&cat);
  animals.push_back(&dog);
  // Note: std::polymorphic could be used to allow the vector to own the
  // animals.

  EXPECT_EQ(make_noise(*animals[0]), "Meow");
  EXPECT_EQ(make_noise(*animals[1]), "Woof");

  // Non-virtual member functions are not found by virtual dispatch.
  EXPECT_NE(animals[0]->identity(), cat.identity());
  EXPECT_NE(animals[1]->identity(), dog.identity());
}

}  // namespace xyz::tutorials::polymorphism_with_inheritance

// Polymorphism with type-erasure.

// Cat and Dog have no common type; we can define a type-erasing wide-pointer to
// determine which member functions to call at run time.

namespace xyz::tutorials::polymorphism_with_type_erasure {

class Cat {
 public:
  std::string_view noise() const { return "Meow"; }
};

class Dog {
 public:
  std::string_view noise() const { return "Woof"; }
};

class AnimalPtr {
  void* data_;
  std::string_view (*noise_func_)(const void* data);

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    noise_func_ = +[](const void* data) -> std::string_view {
      return static_cast<const T*>(data)->noise();
    };
  }

  std::string_view noise() const { return noise_func_(data_); }
};

// `animal` is passed by value as AnimalPtr is small like `std::string_view`.
std::string_view make_noise(AnimalPtr animal) { return animal.noise(); }

TEST(TutorialsPolymorphism, PolymorphismWithTypeErasure) {
  Cat cat;
  Dog dog;

  // We can store pointers to Cat and Dog using our wide pointer type.
  std::vector<AnimalPtr> animals;
  animals.reserve(2);
  animals.emplace_back(&cat);
  animals.emplace_back(&dog);

  EXPECT_EQ(make_noise(animals[0]), "Meow");
  EXPECT_EQ(make_noise(animals[1]), "Woof");
}

}  // namespace xyz::tutorials::polymorphism_with_type_erasure

// When we have multiple functions, we can use a vtable to dispatch them.
// Note that the decision to dispatch functions polymorphically is made by the
// AnimalPtr class, not Cat or Dog classes.
namespace xyz::tutorials::polymorphism_with_type_erasure_vtable {

class Cat {
 public:
  std::string_view noise() const { return "Meow"; }

  std::string_view identity() const { return "Cat"; }
};

class Dog {
 public:
  std::string_view noise() const { return "Woof"; }

  std::string_view identity() const { return "Dog"; }
};

struct vtable {
  std::string_view (*noise_func_)(const void* data);
  std::string_view (*identity_func_)(const void* data);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    constexpr static vtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
        .identity_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->identity();
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return vtable_->noise_func_(data_); }

  std::string_view identity() const { return vtable_->identity_func_(data_); }
};

// Storing the vtable as apointer ensure that `AnimalPtr` is cheap to copy,
// regardless of how many member functions we want to resolve at run time.
std::string_view make_noise(AnimalPtr animal) { return animal.noise(); }

TEST(TutorialsPolymorphism, PolymorphismWithTypeErasureAndVtable) {
  Cat cat;
  Dog dog;

  // We can store pointers to Cat and Dog using our wide pointer type.
  std::vector<AnimalPtr> animals;
  animals.reserve(2);
  animals.emplace_back(&cat);
  animals.emplace_back(&dog);

  EXPECT_EQ(make_noise(animals[0]), "Meow");
  EXPECT_EQ(make_noise(animals[1]), "Woof");

  EXPECT_EQ(animals[0].identity(), "Cat");
  EXPECT_EQ(animals[1].identity(), "Dog");
}

}  // namespace xyz::tutorials::polymorphism_with_type_erasure_vtable

// We can implement inheritance-like intrusive polymorphism manually.
// This is just an exercise, not recommended practice.

namespace xyz::tutorials::manual_intrusive_vtable {

struct vtable {
  std::string_view (*noise_func_)(const void* data);
};

class Animal {
 protected:
  const vtable* vtable_;

 public:
  std::string_view noise() const { return vtable_->noise_func_(this); }
};

class Cat : public Animal {
 public:
  Cat() {
    constexpr static vtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const Cat*>(data)->noise();
            },
    };
    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return "Meow"; }
};

class Dog : public Animal {
 public:
  Dog() {
    constexpr static vtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const Dog*>(data)->noise();
            },
    };
    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return "Woof"; }
};

std::string_view make_noise(const Animal& animal) { return animal.noise(); }

TEST(TutorialsPolymorphism, PolymorphismWithManualInheritance) {
  Cat cat;
  Dog dog;

  // We can store pointers to Cat and Dog as pointers to Animal.
  std::vector<Animal*> animals;
  animals.reserve(2);
  animals.push_back(&cat);
  animals.push_back(&dog);

  EXPECT_EQ(make_noise(*animals[0]), "Meow");
  EXPECT_EQ(make_noise(*animals[1]), "Woof");
}
}  // namespace xyz::tutorials::manual_intrusive_vtable

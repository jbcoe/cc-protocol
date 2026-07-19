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

// Type erasure, from first principles.
//
// Three sections, read top to bottom, each building on the last:
//   1. The problem type erasure solves.
//   2. A vtable of function pointers.
//   3. The eraser object.
//
// Each section defines its own small Circle/Square pair, scoped to a named
// namespace: two trivial, unrelated types that just happen to share a
// draw() method with the same signature.

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// 1. The problem.
//
// A function template can call draw() polymorphically: it works for any T
// with a draw() method. Each instantiation, though, is a distinct function.
// render<Circle> and render<Square> are two different functions with two
// different addresses; there is no single type you could use to store
// "either a Circle or a Square, decided at runtime" and call draw() on it.
// The rest of this file builds one: type erasure.
// ---------------------------------------------------------------------------
namespace section_1 {

class Circle {
 public:
  std::string draw() const { return "○"; }
};

class Square {
 public:
  std::string draw() const { return "□"; }
};

template <typename Shape>
std::string render(const Shape& shape) {
  return shape.draw();
}

TEST(TypeErasureProblem, TemplatesArentUniform) {
  Circle circle;
  Square square;
  EXPECT_EQ(render(circle), "○");
  EXPECT_EQ(render(square), "□");

  // render<Circle> and render<Square> are different types once
  // instantiated, so no single function pointer type could point at both.
  using RenderCircle = std::string (*)(const Circle&);
  using RenderSquare = std::string (*)(const Square&);
  static_assert(!std::is_same_v<RenderCircle, RenderSquare>);
}

}  // namespace section_1

// ---------------------------------------------------------------------------
// 2. A vtable of function pointers.
//
// Circle and Square each get called through a small static object, a
// "vtable", built from exactly the same struct type. The two casts below
// (erasing to const void* at the call site, un-erasing back inside the
// function) are the cast-and-call pattern type erasure relies on.
// ---------------------------------------------------------------------------
namespace section_2 {

class Circle {
 public:
  std::string draw() const { return "○"; }
};

class Square {
 public:
  std::string draw() const { return "□"; }
};

// One function pointer, one method. A real interface with several methods
// would just repeat this shape once per method it needs to expose.
struct draw_vtable {
  std::string (*draw)(const void* erased);
};

// One static instance per concrete type. Each is initialized with a
// function that does the two casts: the incoming pointer arrives already
// erased to const void* (the caller did that), and this function un-erases
// it back to the one concrete type it actually knows how to handle, then
// calls draw() on it normally.
template <typename Shape>
inline constexpr draw_vtable vtable_for = {
    .draw = [](const void* erased) -> std::string {
      return static_cast<const Shape*>(erased)->draw();
    }};

TEST(TypeErasureVtable, SameVtableDifferentTypes) {
  Circle circle;
  Square square;

  // Two different concrete objects are called through the same vtable
  // type (draw_vtable) via the same cast-and-call pattern; only the
  // static instance and the erased pointer differ.
  EXPECT_EQ(vtable_for<Circle>.draw(&circle), "○");
  EXPECT_EQ(vtable_for<Square>.draw(&square), "□");

  // Both static vtable instances share one C++ type, unlike the two
  // distinct render<T> instantiations from section 1.
  static_assert(std::is_same_v<decltype(vtable_for<Circle>),
                               decltype(vtable_for<Square>)>);
}

}  // namespace section_2

// ---------------------------------------------------------------------------
// 3. The eraser object.
//
// Package section 2's erased pointer and vtable pointer into one small
// class, with a constructor template that captures whatever concrete type
// it's given. Circle and Square can both be stored behind the result, and both
// go in one std::vector. This is the minimal, one-method analogue of
// xyz::protocol_view: a non-owning, type-erased handle with forwarding
// methods that dispatch through the vtable.
// ---------------------------------------------------------------------------
namespace section_3 {

class Circle {
 public:
  std::string draw() const { return "○"; }
};

class Square {
 public:
  std::string draw() const { return "□"; }
};

struct draw_vtable {
  std::string (*draw)(const void* erased);
};

template <typename Shape>
inline constexpr draw_vtable vtable_for = {
    .draw = [](const void* erased) -> std::string {
      return static_cast<const Shape*>(erased)->draw();
    }};

// erased_shape only stores a pointer to a Shape it doesn't own. The
// concrete object (the Circle or Square passed to the constructor) must
// outlive every erased_shape that refers to it.
class erased_shape {
 public:
  template <typename Shape>
  explicit erased_shape(const Shape& shape)
      : erased_(&shape), vtable_(&vtable_for<Shape>) {}

  std::string draw() const { return vtable_->draw(erased_); }

 private:
  const void* erased_;
  const draw_vtable* vtable_;
};

TEST(TypeErasureEraser, UnifiesUnrelatedTypes) {
  Circle circle;
  Square square;

  erased_shape erased_circle(circle);
  erased_shape erased_square(square);

  EXPECT_EQ(erased_circle.draw(), "○");
  EXPECT_EQ(erased_square.draw(), "□");

  // Both are the same C++ type, unlike Circle and Square themselves.
  static_assert(
      std::is_same_v<decltype(erased_circle), decltype(erased_square)>);
}

TEST(TypeErasureEraser, BothTypesInOneVector) {
  Circle circle;
  Square square;

  // One vector holds both a Circle and a Square, dispatched uniformly;
  // neither class was touched or related to the other.
  std::vector<erased_shape> shapes;
  shapes.emplace_back(circle);
  shapes.emplace_back(square);

  std::vector<std::string> drawn;
  for (const erased_shape& shape : shapes) {
    drawn.push_back(shape.draw());
  }

  EXPECT_EQ(drawn, (std::vector<std::string>{"○", "□"}));
}

}  // namespace section_3

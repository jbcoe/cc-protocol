/* Copyright (c) 2016 The Value Types Authors. All Rights Reserved.

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

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

#include "xyz/detail/small_buffer.hh"
#include "xyz/protocol.hh"
#include "xyz/tests/test_helpers.h"

// `protocol` as the element type of the standard containers. A container
// moves, copies, swaps and assigns its elements, and passes its allocator to
// them.

namespace xyz::tests {
namespace {

struct Shape {
  int area() const;
  void scale(int factor);
};

// Stored in the small buffer.
struct Square {
  int side_;

  constexpr int area() const { return side_ * side_; }

  constexpr void scale(int factor) { side_ *= factor; }
};

// Too large for the small buffer, so allocated.
struct Rectangle {
  int width_;
  int height_;
  [[maybe_unused]] TooLargeForSmallBuffer padding_{};

  constexpr int area() const { return width_ * height_; }

  constexpr void scale(int factor) {
    width_ *= factor;
    height_ *= factor;
  }
};

using Alloc = std::allocator<std::byte>;

static_assert(detail::fits_in_small_buffer_v<Square, Alloc>);
static_assert(!detail::fits_in_small_buffer_v<Rectangle, Alloc>);

using ShapeProtocol = protocol<Shape>;
using PmrShapeProtocol =
    protocol<Shape, std::pmr::polymorphic_allocator<std::byte>>;

template <typename Shapes>
std::vector<int> areas_of(const Shapes& shapes) {
  std::vector<int> areas;
  areas.reserve(shapes.size());
  for (const auto& shape : shapes) {
    areas.push_back(shape.area());
  }
  return areas;
}

// Counts the copies, moves and live objects of `Tracker`s.
struct Counts {
  std::size_t copies = 0;
  std::size_t moves = 0;
  std::size_t live = 0;

  Counts() = default;
  Counts(const Counts&) = delete ("Unused");
  Counts(Counts&&) = delete ("Unused");
  Counts& operator=(const Counts&) = delete ("Unused");
  Counts& operator=(Counts&&) = delete ("Unused");

  ~Counts() { EXPECT_EQ(live, 0); }
};

// Conforms to `Shape`. `PaddingSize` bytes of padding decide whether it fits
// in the small buffer.
template <std::size_t PaddingSize>
class Tracker {
  Counts* counts_;
  int side_;
  [[maybe_unused]] std::array<std::byte, PaddingSize> padding_{};

 public:
  Tracker(Counts* counts, int side) noexcept : counts_(counts), side_(side) {
    ++counts_->live;
  }

  Tracker(const Tracker& other) : counts_(other.counts_), side_(other.side_) {
    ++counts_->live;
    ++counts_->copies;
  }

  Tracker(Tracker&& other) noexcept
      : counts_(other.counts_), side_(other.side_) {
    ++counts_->live;
    ++counts_->moves;
  }

  Tracker& operator=(const Tracker&) = delete ("Unused");
  Tracker& operator=(Tracker&&) = delete ("Unused");

  ~Tracker() { --counts_->live; }

  int area() const { return side_ * side_; }

  void scale(int factor) { side_ *= factor; }
};

using SmallTracker = Tracker<0>;
using LargeTracker = Tracker<detail::small_buffer_size>;

static_assert(detail::fits_in_small_buffer_v<SmallTracker, Alloc>);
static_assert(!detail::fits_in_small_buffer_v<LargeTracker, Alloc>);

// ---------------------------------------------------------------------------
// std::vector.
// ---------------------------------------------------------------------------

TEST(ContainerTest, VectorReallocationMovesAnObjectInTheSmallBuffer) {
  Counts counts;
  std::vector<ShapeProtocol> shapes;
  shapes.emplace_back(std::in_place_type<SmallTracker>, &counts, 2);

  shapes.reserve(shapes.capacity() + 1);

  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(counts.live, 1);
  EXPECT_EQ(shapes.front().area(), 4);
}

TEST(ContainerTest, VectorReallocationTakesAnAllocatedObject) {
  Counts counts;
  std::vector<ShapeProtocol> shapes;
  shapes.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);

  shapes.reserve(shapes.capacity() + 1);

  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 0);
  EXPECT_EQ(counts.live, 1);
  EXPECT_EQ(shapes.front().area(), 4);
}

TEST(ContainerTest, VectorCopyCopiesEachObject) {
  std::vector<ShapeProtocol> shapes;
  shapes.emplace_back(Square{2});
  shapes.emplace_back(Rectangle{2, 3});

  std::vector<ShapeProtocol> copies = shapes;
  for (ShapeProtocol& copy : copies) {
    copy.scale(2);
  }

  EXPECT_EQ(areas_of(shapes), (std::vector<int>{4, 6}));
  EXPECT_EQ(areas_of(copies), (std::vector<int>{16, 24}));
}

TEST(ContainerTest, VectorEraseAndInsertShiftElements) {
  Counts counts;
  std::vector<ShapeProtocol> shapes;
  shapes.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  shapes.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);
  shapes.emplace_back(std::in_place_type<SmallTracker>, &counts, 3);
  shapes.emplace_back(std::in_place_type<LargeTracker>, &counts, 4);

  shapes.erase(shapes.begin() + 1);
  EXPECT_EQ(counts.live, 3);
  EXPECT_EQ(areas_of(shapes), (std::vector<int>{1, 9, 16}));

  shapes.insert(shapes.begin(),
                ShapeProtocol(std::in_place_type<LargeTracker>, &counts, 5));
  EXPECT_EQ(counts.live, 4);
  EXPECT_EQ(areas_of(shapes), (std::vector<int>{25, 1, 9, 16}));

  EXPECT_EQ(counts.copies, 0);
}

TEST(ContainerTest, VectorSortsByProjection) {
  Counts counts;
  std::vector<ShapeProtocol> shapes;
  shapes.emplace_back(std::in_place_type<LargeTracker>, &counts, 4);
  shapes.emplace_back(std::in_place_type<SmallTracker>, &counts, 2);
  shapes.emplace_back(std::in_place_type<LargeTracker>, &counts, 3);
  shapes.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);

  std::ranges::sort(shapes, {},
                    [](const ShapeProtocol& shape) { return shape.area(); });

  EXPECT_EQ(areas_of(shapes), (std::vector<int>{1, 4, 9, 16}));
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.live, 4);
}

TEST(ContainerTest, VectorInConstantEvaluation) {
  static_assert([] consteval {
    std::vector<ShapeProtocol> shapes;
    shapes.emplace_back(Square{1});
    shapes.emplace_back(Rectangle{2, 3});
    shapes.emplace_back(Square{4});
    shapes.erase(shapes.begin());

    std::vector<ShapeProtocol> copies = shapes;
    std::ranges::reverse(copies);

    return copies.size() == 2 &&
           protocol_cast<Square>(&copies.front())->area() == 16 &&
           protocol_cast<Rectangle>(&copies.back())->area() == 6;
  }());
}

// ---------------------------------------------------------------------------
// std::map. `protocol` has no default constructor, so `operator[]` is not
// available.
// ---------------------------------------------------------------------------

TEST(ContainerTest, MapMappedValue) {
  std::map<std::string, ShapeProtocol> shapes;
  shapes.emplace("square", Square{2});
  shapes.try_emplace("rectangle", Rectangle{2, 3});

  shapes.insert_or_assign("square", ShapeProtocol(Rectangle{4, 5}));

  EXPECT_EQ(shapes.at("square").area(), 20);
  EXPECT_EQ(shapes.at("rectangle").area(), 6);
}

// ---------------------------------------------------------------------------
// std::pmr::vector. The container constructs each element with its own
// allocator.
// ---------------------------------------------------------------------------

static_assert(std::uses_allocator_v<
              PmrShapeProtocol, std::pmr::polymorphic_allocator<std::byte>>);

TEST(ContainerTest, PmrVectorPassesItsAllocatorToEachElement) {
  std::pmr::monotonic_buffer_resource resource;
  std::pmr::vector<PmrShapeProtocol> shapes(&resource);
  shapes.emplace_back(Square{2});
  shapes.emplace_back(Rectangle{2, 3});

  for (const PmrShapeProtocol& shape : shapes) {
    EXPECT_EQ(shape.get_allocator().resource(), &resource);
  }
}

TEST(ContainerTest, PmrVectorCopyWithAnotherAllocator) {
  std::pmr::monotonic_buffer_resource resource;
  std::pmr::vector<PmrShapeProtocol> shapes(&resource);
  shapes.emplace_back(Square{2});
  shapes.emplace_back(Rectangle{2, 3});

  std::pmr::monotonic_buffer_resource other_resource;
  std::pmr::vector<PmrShapeProtocol> copies(shapes, &other_resource);

  for (const PmrShapeProtocol& copy : copies) {
    EXPECT_EQ(copy.get_allocator().resource(), &other_resource);
  }
  EXPECT_EQ(areas_of(copies), (std::vector<int>{4, 6}));
  EXPECT_EQ(areas_of(shapes), (std::vector<int>{4, 6}));
}

TEST(ContainerTest, PmrVectorMoveWithAnotherAllocator) {
  std::pmr::monotonic_buffer_resource resource;
  std::pmr::vector<PmrShapeProtocol> shapes(&resource);
  shapes.emplace_back(Square{2});
  shapes.emplace_back(Rectangle{2, 3});

  // The allocators differ, so each element is moved rather than the
  // container's storage taken.
  std::pmr::monotonic_buffer_resource other_resource;
  std::pmr::vector<PmrShapeProtocol> moved(std::move(shapes), &other_resource);

  for (const PmrShapeProtocol& shape : moved) {
    EXPECT_EQ(shape.get_allocator().resource(), &other_resource);
  }
  EXPECT_EQ(areas_of(moved), (std::vector<int>{4, 6}));
}

}  // namespace
}  // namespace xyz::tests

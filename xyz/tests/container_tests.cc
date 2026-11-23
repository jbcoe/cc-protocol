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
#include <scoped_allocator>
#include <string>
#include <utility>
#include <vector>

#include "xyz/detail/small_buffer.hh"
#include "xyz/protocol.hh"
#include "xyz/tests/tracker.h"
#include "xyz/tests/tracking_allocator.h"

// `protocol` as the element type of the standard containers. A container
// moves, copies, swaps and assigns its elements, and passes its allocator to
// them.

namespace xyz::tests {
namespace {

struct Interface {
  int value() const;
};

using Alloc = std::allocator<std::byte>;

static_assert(detail::fits_in_small_buffer_v<SmallTracker, Alloc>);
static_assert(!detail::fits_in_small_buffer_v<LargeTracker, Alloc>);

using Protocol = protocol<Interface>;

template <typename Protocols>
std::vector<int> values_of(const Protocols& protocols) {
  std::vector<int> values;
  values.reserve(protocols.size());
  for (const auto& element : protocols) {
    values.push_back(element.value());
  }
  return values;
}

// ---------------------------------------------------------------------------
// std::vector.
// ---------------------------------------------------------------------------

TEST(ContainerTest, VectorReallocationMovesAnObjectInTheSmallBuffer) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);

  protocols.reserve(protocols.capacity() + 1);

  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(counts.live, 1);
  EXPECT_EQ(protocols.front().value(), 1);
}

TEST(ContainerTest, VectorReallocationTakesAnAllocatedObject) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 1);

  protocols.reserve(protocols.capacity() + 1);

  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 0);
  EXPECT_EQ(counts.live, 1);
  EXPECT_EQ(protocols.front().value(), 1);
}

TEST(ContainerTest, VectorCopyCopiesEachObject) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);

  std::vector<Protocol> copies = protocols;

  EXPECT_EQ(counts.copies, 2);
  EXPECT_EQ(counts.live, 4);
  EXPECT_EQ(values_of(copies), (std::vector<int>{1, 2}));
}

TEST(ContainerTest, VectorCopyAssignmentCopiesEachObject) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);
  std::vector<Protocol> assigned;
  assigned.emplace_back(std::in_place_type<LargeTracker>, &counts, 3);
  assigned.emplace_back(std::in_place_type<SmallTracker>, &counts, 4);

  assigned = protocols;

  EXPECT_EQ(counts.copies, 2);
  EXPECT_EQ(counts.live, 4);
  EXPECT_EQ(values_of(assigned), (std::vector<int>{1, 2}));
}

TEST(ContainerTest, VectorEraseAndInsertShiftElements) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 3);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 4);

  protocols.erase(protocols.begin() + 1);
  EXPECT_EQ(counts.live, 3);
  EXPECT_EQ(values_of(protocols), (std::vector<int>{1, 3, 4}));

  protocols.insert(protocols.begin(),
                   Protocol(std::in_place_type<LargeTracker>, &counts, 5));
  EXPECT_EQ(counts.live, 4);
  EXPECT_EQ(values_of(protocols), (std::vector<int>{5, 1, 3, 4}));

  EXPECT_EQ(counts.copies, 0);
}

TEST(ContainerTest, VectorReverseSwapsElements) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 2);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 3);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 4);

  std::ranges::reverse(protocols);

  EXPECT_EQ(values_of(protocols), (std::vector<int>{4, 3, 2, 1}));
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.live, 4);
}

TEST(ContainerTest, VectorSortsByProjection) {
  Counts counts;
  std::vector<Protocol> protocols;
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 4);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 2);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 3);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);

  // `noexcept` so that libc++ does not initialize a variable template with an
  // expression that clang-tidy's cert-err58-cpp takes to throw.
  std::ranges::sort(protocols, {}, [](const Protocol& element) noexcept {
    return element.value();
  });

  EXPECT_EQ(values_of(protocols), (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.live, 4);
}

TEST(ContainerTest, VectorOfProtocolsThatCannotBeCopied) {
  struct MoveOnlyInterface {
    MoveOnlyInterface(const MoveOnlyInterface&) = delete;
    MoveOnlyInterface(MoveOnlyInterface&&) = default;

    MoveOnlyInterface& operator=(const MoveOnlyInterface&) = delete;
    MoveOnlyInterface& operator=(MoveOnlyInterface&&) = default;

    ~MoveOnlyInterface() = default;

    int value() const;
  };

  using MoveOnlyProtocol = protocol<MoveOnlyInterface>;
  static_assert(!std::is_copy_constructible_v<MoveOnlyProtocol>);

  Counts counts;
  std::vector<MoveOnlyProtocol> protocols;
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 3);

  protocols.reserve(protocols.capacity() + 1);
  protocols.erase(protocols.begin());
  std::ranges::reverse(protocols);

  EXPECT_EQ(values_of(protocols), (std::vector<int>{3, 2}));
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.live, 2);
}

TEST(ContainerTest, VectorInConstantEvaluation) {
  struct Small {
    int value_;

    constexpr int value() const { return value_; }
  };

  struct Large {
    int value_;
    [[maybe_unused]] std::array<std::byte, detail::small_buffer_size>
        padding_{};

    constexpr int value() const { return value_; }
  };

  static_assert([] consteval {
    std::vector<Protocol> protocols;
    protocols.emplace_back(Small{1});
    protocols.emplace_back(Large{2});
    protocols.emplace_back(Small{3});
    protocols.erase(protocols.begin());

    std::vector<Protocol> copies = protocols;
    std::ranges::reverse(copies);

    return copies.size() == 2 &&
           protocol_cast<Small>(&copies.front())->value() == 3 &&
           protocol_cast<Large>(&copies.back())->value() == 2;
  }());
}

// ---------------------------------------------------------------------------
// std::map. `protocol` has no default constructor, so `operator[]` is not
// available.
// ---------------------------------------------------------------------------

TEST(ContainerTest, MapMappedValue) {
  Counts counts;
  std::map<std::string, Protocol> protocols;
  protocols.try_emplace("small", std::in_place_type<SmallTracker>, &counts, 1);
  protocols.try_emplace("large", std::in_place_type<LargeTracker>, &counts, 2);

  protocols.insert_or_assign(
      "small", Protocol(std::in_place_type<LargeTracker>, &counts, 3));

  EXPECT_EQ(protocols.at("small").value(), 3);
  EXPECT_EQ(protocols.at("large").value(), 2);
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.live, 2);
}

// ---------------------------------------------------------------------------
// Containers that pass their allocator to their elements.
// ---------------------------------------------------------------------------

TEST(ContainerTest, ScopedAllocatorVectorPassesItsAllocatorToEachElement) {
  using TrackingProtocol = protocol<Interface, TrackingAllocator<std::byte>>;
  using VectorAllocator =
      std::scoped_allocator_adaptor<TrackingAllocator<TrackingProtocol>>;

  AllocationCounter counter;
  Counts counts;
  std::vector<TrackingProtocol, VectorAllocator> protocols(VectorAllocator(
      counter.allocator<TrackingAllocator<TrackingProtocol>>()));
  protocols.reserve(2);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);

  // The vector's storage and the large object. The small object is in the
  // small buffer.
  EXPECT_EQ(counter.allocs, 2);
  for (const TrackingProtocol& element : protocols) {
    EXPECT_EQ(element.get_allocator(), counter.allocator());
  }
}

// Counts the allocations that it passes on to the default resource.
struct CountingResource : std::pmr::memory_resource {
  std::size_t allocations = 0;
  std::size_t deallocations = 0;

  CountingResource() = default;
  CountingResource(const CountingResource&) = delete ("Unused");
  CountingResource(CountingResource&&) = delete ("Unused");
  CountingResource& operator=(const CountingResource&) = delete ("Unused");
  CountingResource& operator=(CountingResource&&) = delete ("Unused");

  ~CountingResource() override { EXPECT_EQ(allocations, deallocations); }

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    ++allocations;
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }

  void do_deallocate(void* pointer, std::size_t bytes,
                     std::size_t alignment) override {
    ++deallocations;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }

  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

// Nothing is stored in the small buffer with this allocator, as its
// `construct` can throw.
using PmrProtocol = protocol<Interface, std::pmr::polymorphic_allocator<>>;

static_assert(
    std::uses_allocator_v<PmrProtocol, std::pmr::polymorphic_allocator<>>);

TEST(ContainerTest, PmrVectorPassesItsAllocatorToEachElement) {
  CountingResource resource;
  Counts counts;
  std::pmr::vector<PmrProtocol> protocols(&resource);
  protocols.reserve(2);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);

  // The vector's storage and both objects.
  EXPECT_EQ(resource.allocations, 3);
  for (const PmrProtocol& element : protocols) {
    EXPECT_EQ(element.get_allocator().resource(), &resource);
  }
}

TEST(ContainerTest, PmrVectorCopyWithAnotherAllocator) {
  CountingResource resource;
  CountingResource other_resource;
  Counts counts;
  std::pmr::vector<PmrProtocol> protocols(&resource);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);

  std::pmr::vector<PmrProtocol> copies(protocols, &other_resource);

  // The vector's storage and both objects.
  EXPECT_EQ(other_resource.allocations, 3);
  for (const PmrProtocol& copy : copies) {
    EXPECT_EQ(copy.get_allocator().resource(), &other_resource);
  }
  EXPECT_EQ(values_of(copies), (std::vector<int>{1, 2}));
  EXPECT_EQ(counts.copies, 2);
  EXPECT_EQ(counts.live, 4);
}

TEST(ContainerTest, PmrVectorMoveWithAnotherAllocator) {
  CountingResource resource;
  CountingResource other_resource;
  Counts counts;
  std::pmr::vector<PmrProtocol> protocols(&resource);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);

  // The allocators differ, so each object is moved to storage from the other
  // allocator rather than the vector's storage being taken.
  std::pmr::vector<PmrProtocol> moved(std::move(protocols), &other_resource);

  for (const PmrProtocol& element : moved) {
    EXPECT_EQ(element.get_allocator().resource(), &other_resource);
  }
  EXPECT_EQ(values_of(moved), (std::vector<int>{1, 2}));
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 2);
  EXPECT_EQ(counts.live, 2);
}

TEST(ContainerTest, PmrVectorCopyAssignmentWithAnotherAllocator) {
  CountingResource resource;
  CountingResource other_resource;
  Counts counts;
  std::pmr::vector<PmrProtocol> protocols(&resource);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);
  std::pmr::vector<PmrProtocol> assigned(&other_resource);
  assigned.emplace_back(std::in_place_type<LargeTracker>, &counts, 3);
  assigned.emplace_back(std::in_place_type<SmallTracker>, &counts, 4);

  assigned = protocols;

  for (const PmrProtocol& element : assigned) {
    EXPECT_EQ(element.get_allocator().resource(), &other_resource);
  }
  EXPECT_EQ(values_of(assigned), (std::vector<int>{1, 2}));
  EXPECT_EQ(counts.copies, 2);
  EXPECT_EQ(counts.live, 4);
}

TEST(ContainerTest, PmrVectorMoveAssignmentWithAnotherAllocator) {
  CountingResource resource;
  CountingResource other_resource;
  Counts counts;
  std::pmr::vector<PmrProtocol> protocols(&resource);
  protocols.emplace_back(std::in_place_type<SmallTracker>, &counts, 1);
  protocols.emplace_back(std::in_place_type<LargeTracker>, &counts, 2);
  std::pmr::vector<PmrProtocol> assigned(&other_resource);
  assigned.emplace_back(std::in_place_type<LargeTracker>, &counts, 3);
  assigned.emplace_back(std::in_place_type<SmallTracker>, &counts, 4);

  assigned = std::move(protocols);

  for (const PmrProtocol& element : assigned) {
    EXPECT_EQ(element.get_allocator().resource(), &other_resource);
  }
  EXPECT_EQ(values_of(assigned), (std::vector<int>{1, 2}));
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 2);
  EXPECT_EQ(counts.live, 2);
}

TEST(ContainerTest, PmrMapPassesItsAllocatorToEachMappedValue) {
  CountingResource resource;
  CountingResource other_resource;
  Counts counts;
  std::pmr::map<int, PmrProtocol> protocols(&resource);
  protocols.try_emplace(1, std::in_place_type<SmallTracker>, &counts, 1);
  protocols.try_emplace(2, std::in_place_type<LargeTracker>, &counts, 2);

  // The assigned protocol's allocator differs, so its object is moved to
  // storage from the map's allocator.
  protocols.insert_or_assign(
      1, PmrProtocol(std::allocator_arg, &other_resource,
                     std::in_place_type<LargeTracker>, &counts, 3));

  for (const auto& [key, element] : protocols) {
    EXPECT_EQ(element.get_allocator().resource(), &resource);
  }
  EXPECT_EQ(protocols.at(1).value(), 3);
  EXPECT_EQ(protocols.at(2).value(), 2);
  EXPECT_EQ(counts.copies, 0);
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(counts.live, 2);
}

}  // namespace
}  // namespace xyz::tests

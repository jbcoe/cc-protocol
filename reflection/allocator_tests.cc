#include <gtest/gtest.h>

#include "protocol.h"
#include "tagged_allocator.h"
#include "tracking_allocator.h"

#include <cstddef>
#include <vector>

struct Blank {};

struct NonCopyable {
  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;

  NonCopyable(NonCopyable&&) = default;
  NonCopyable& operator=(NonCopyable&&) = default;

  ~NonCopyable() = default;
};

// IMPORTANT: Allocation counting assumes that protocol is always deferring
// to its allocator when creating a value. In reality, it should probably
// implement small buffer optimization, and therefore we may find there are
// fewer allocations than these tests suggest. To sidestep this, we can use
// a type that is guaranteed to be larger than protocol.
struct Tester {
  int val;
  Tester(int value) : val(value) {}

  Tester(std::initializer_list<int>) {}
private:
  using P = xyz::protocol<Blank, xyz::TrackingAllocator<std::byte>>;
  std::array<std::byte, sizeof(P)> padding;
};

TEST(ProtocolTest, ConstructionAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
    xyz::protocol<Blank, decltype(alloc)> p{std::allocator_arg, alloc, Tester{15}};
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, InPlaceConstructionAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
    xyz::protocol<Blank, decltype(alloc)> p{std::allocator_arg, alloc,
                                            std::in_place_type<Tester>, 15};
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, InitListConstructionAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
    xyz::protocol<Blank, decltype(alloc)> p{std::allocator_arg, alloc,
                                            std::in_place_type<Tester>, {1, 2, 3}};
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, CopyConstructionAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
    xyz::protocol<Blank, decltype(alloc)> p1{std::allocator_arg, alloc, Tester{25}};
    xyz::protocol p2{p1};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, CopyConstructionEqualAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc1{&allocs, &deallocs};
    xyz::protocol<Blank, decltype(alloc1)> p1{std::allocator_arg, alloc, Tester{100}};
    
    xyz::TrackingAllocator<std::byte> alloc2{&allocs, &deallocs};
    xyz::protocol<Blank, decltype(alloc2)> p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, CopyConstructionUnequalAllocs) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};
  {
    xyz::TrackingAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<Blank, decltype(alloc1)> p1{std::allocator_arg, alloc, Tester{100}};
    
    xyz::TrackingAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<Blank, decltype(alloc2)> p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);
    EXPECT_EQ(deallocs1, 0);
    EXPECT_EQ(deallocs2, 0);
  }
  EXPECT_EQ(dealloc1, 1);
  EXPECT_EQ(dealloc2, 1);
}

TEST(ProtocolTest, CopyAssignmentAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
    
    xyz::protocol<Blank, decltype(alloc)> p1{std::allocator_arg, alloc, Tester{30}};
    xyz::protocol<Blank, decltype(alloc)> p2{Tester{40}};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    p1 = p2;
    EXPECT_EQ(allocs, 3);
    EXPECT_EQ(deallocs, 1);
  }
  EXPECT_EQ(allocs, 3);
  EXPECT_EQ(deallocs, 3);
}

Test(ProtocolTest, MoveConstructionAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
    
    xyz::protocol<Blank, decltype(alloc)> p1{std::allocator_arg, alloc, Tester{50}};
    xyz::protocol p2{std::move(p1)};
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}
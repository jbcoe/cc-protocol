#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "protocol.h"
#include "tagged_allocator.h"
#include "tracking_allocator.h"

namespace {

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

using TestAlloc = xyz::TrackingAllocator<std::byte>;
using TestProtocol = xyz::protocol<Blank, TestAlloc>;

struct Tester {
  int val;

  Tester(int value) noexcept : val(value) {}

  Tester(std::initializer_list<int>) noexcept {}

 private:
  std::array<std::byte, sizeof(P)> padding;
};

TEST(ProtocolTest, ConstructionAllocs) {
  static_assert(
      not std::is_nothrow_constructible_v<TestProtocol, std::allocator_arg_t,
                                          TestAlloc, Tester>);

  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc{&allocs, &deallocs};
    TestProtocol p{std::allocator_arg, alloc, Tester{15}};

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
    TestAlloc alloc{&allocs, &deallocs};
    TestProtocol p{std::allocator_arg, alloc, std::in_place_type<Tester>, 15};

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
    TestAlloc alloc{&allocs, &deallocs};
    TestProtocol p{
        std::allocator_arg, alloc, std::in_place_type<Tester>, {1, 2, 3}};

    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, CopyConstructionAllocs) {
  static_assert(not std::is_nothrow_copy_constructible_v<TestProtocol>);

  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc, Tester{25}};
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
    TestAlloc alloc1{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs, &deallocs};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

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
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs2, &deallocs2};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);
    EXPECT_EQ(deallocs1, 0);
    EXPECT_EQ(deallocs2, 0);
  }
  EXPECT_EQ(deallocs1, 1);
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, CopyAssignmentEqualAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{30}};
    TestProtocol p2{Tester{40}};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    p1 = p2;
    EXPECT_EQ(allocs, 3);
    EXPECT_EQ(deallocs, 1);
  }
  EXPECT_EQ(allocs, 3);
  EXPECT_EQ(deallocs, 3);
}

TEST(ProtocolTest, CopyAssignmentUnequalAllocs) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};
  {
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs2, &deallocs2};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    p1 = p2;

    EXPECT_EQ(allocs1, 2);
    EXPECT_EQ(allocs2, 1);
  }
  EXPECT_EQ(deallocs1, 2);
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, MoveConstructionAllocs) {
  static_assert(std::is_nothrow_move_constructible_v<TestProtocol>);

  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{50}};
    xyz::protocol p2{std::move(p1)};
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, MoveConstructionEqualAllocs) {
  static_assert(not std::is_nothrow_constructible_v<TestProtocol, std::allocator_arg_t, TestAlloc, TestProtocol&&>);

  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc1{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs, &deallocs};
    TestProtocol p2{std::allocator_arg, alloc2, std::move(p1)};

    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, MoveConstructionUnequalAllocs) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};
  {
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs2, &deallocs2};
    TestProtocol p2{std::allocator_arg, alloc2, std::move(p1)};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);
    EXPECT_EQ(deallocs1, 0);
    EXPECT_EQ(deallocs2, 0);
  }
  EXPECT_EQ(deallocs1, 1);
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, MoveAssignmentEqualAllocs) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{30}};
    TestProtocol p2{Tester{40}};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    p1 = std::move(p2);
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 1);
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, MoveAssignmentUnequalAllocs) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};
  {
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs2, &deallocs2};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    p1 = std::move(p2);

    EXPECT_EQ(allocs1, 2);
    EXPECT_EQ(allocs2, 1);
  }
  EXPECT_EQ(deallocs1, 2);
  EXPECT_EQ(deallocs2, 1);
}

}
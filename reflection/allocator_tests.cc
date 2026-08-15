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

using TestAlloc = xyz::TrackingAllocator<std::byte>;
using TestProtocol = xyz::protocol<Blank, TestAlloc>;

struct Tester {
  int val;

  Tester(int value) noexcept : val(value) {}

  Tester(std::initializer_list<int>) noexcept {}

 private:
  // protocol should probably
  // implement small buffer optimization, and therefore we may find there are
  // fewer allocations than these tests suggest. To sidestep this, we can use
  // a type that is guaranteed to be larger than protocol.
  std::array<std::byte, sizeof(P)> padding;
};

TEST(ProtocolTest, Construction) {
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

TEST(ProtocolTest, InPlaceConstruction) {
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

TEST(ProtocolTest, InitListConstruction) {
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

TEST(ProtocolTest, CopyConstruction) {
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

TEST(ProtocolTest, CopyConstructionEqual) {
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

TEST(ProtocolTest, CopyConstructionUnequal) {
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

TEST(ProtocolTest, CopyAssignmentEqual) {
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

TEST(ProtocolTest, CopyAssignmentUnequal) {
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

TEST(ProtocolTest, MoveConstruction) {
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

TEST(ProtocolTest, MoveConstructionEqual) {
  static_assert(
      not std::is_nothrow_constructible_v<TestProtocol, std::allocator_arg_t,
                                          TestAlloc, TestProtocol&&>);

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

TEST(ProtocolTest, MoveConstructionUnequal) {
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
    EXPECT_EQ(deallocs1, 1);
    EXPECT_EQ(deallocs2, 0);
  }
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, MoveAssignmentEqual) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{30}};
    TestProtocol p2{std::allocator_arg, alloc, Tester{40}};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    p1 = std::move(p2);
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 1);
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, MoveAssignmentUnequal) {
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

TEST(ProtocolTest, SwapEqual) {
  unsigned allocs{};
  unsigned deallocs{};
  {
    TestAlloc alloc1{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs, &deallocs};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs, 2);

    std::swap(p1, p2);

    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    EXPECT_EQ(p1.get_allocator(), alloc1);
    EXPECT_EQ(p2.get_allocator(), alloc2);
  }
  EXPECT_EQ(deallocs, 2);
}

// Tests that swap fails gracefully with an assert in debug mode.
// In release mode, this is undefined behavior.
#if (defined(_MSC_VER) && defined(_DEBUG)) || (!defined(NDEBUG))
TEST(ProtocolTest, SwapUnequal) {
  unsigned allocs1{};
  unsigned deallocs1{};
  TestAlloc alloc1{&allocs1, &deallocs1};
  TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

  unsigned allocs2{};
  unsigned deallocs2{};
  TestAlloc alloc2{&allocs2, &deallocs2};
  TestProtocol p2{std::allocator_arg, alloc2, p1};

  using std::swap;
  EXPECT_DEATH(swap(p1, p2));
}
#endif

template <typename T>
struct PoccaAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_copy_assignment = std::true_type;

  template <typename Other>
  struct rebind {
    using other = PoccaAllocator<Other>;
  }
};

TEST(ProtocolTest, CopyAssignmentUnequalPocca) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};

  {
    PoccaAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<Blank, PoccaAllocator<std::byte>> p1{std::allocator_arg,
                                                       alloc1, Tester{0}};

    PoccaAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<Blank, PoccaAllocator<std::byte>> p2{std::allocator_arg,
                                                       alloc2, Tester{0}};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    p1 = p2;
    EXPECT_EQ(allocs1, 2);
    EXPECT_EQ(deallocs1, 1);
    EXPECT_EQ(deallocs2, 1);
  }

  EXPECT_EQ(deallocs1, 2);
}

template <typename T>
struct PocmaAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_move_assignment = std::true_type;

  template <typename Other>
  struct rebind {
    using other = PocmaAllocator<Other>;
  }
};

TEST(ProtocolTest, MoveAssignmentUnequalPocma) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};

  {
    PocmaAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<Blank, PocmaAllocator<std::byte>> p1{std::allocator_arg,
                                                       alloc1, Tester{0}};

    PocmaAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<Blank, PocmaAllocator<std::byte>> p2{std::allocator_arg,
                                                       alloc2, Tester{10}};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    p1 = std::move(p2);
    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(deallocs1, 1);

    EXPECT_EQ(allocs2, 1);
    EXPECT_EQ(deallocs2, 0);
  }

  EXPECT_EQ(deallocs2, 1);
}

template <typename T>
struct PocsAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_swap = std::true_type;

  template <typename Other>
  struct rebind {
    using other = PocsAllocator<Other>;
  }
};

TEST(ProtocolTest, SwapUnequalPocs) {
  unsigned allocs1{};
  unsigned deallocs1{};

  unsigned allocs2{};
  unsigned deallocs2{};
  {
    PocsAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<Blank, PocsAllocator<std::byte>> p1{std::allocator_arg, alloc1,
                                                       Tester{0}};
  
    PocsAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<Blank, PocsAllocator<std::byte>> p2{std::allocator_arg, alloc2,
                                                       Tester{10}};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    using std::swap;
    swap(alloc1, alloc2);

    EXPECT_EQ(p1.get_allocator(), alloc2);
    EXPECT_EQ(p2.get_allocator(), alloc1);

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);
  }

  EXPECT_EQ(deallocs1, 1);
  EXPECT_EQ(deallocs2, 1);
}

// TODO: Add exception checks

// TODO: Add tests with PMR / STL

}  // namespace
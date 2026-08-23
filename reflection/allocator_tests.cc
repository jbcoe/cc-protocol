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

#include <array>
#include <cstddef>
#include <new>
#include <type_traits>

#include "protocol.hh"
#include "tagged_allocator.h"
#include "tracking_allocator.h"

// Workaround to move protocol into xyz namespace
namespace xyz {
using reflection::protocol;
}

namespace {

// A simple interface to test protocol with.
struct IntLike {
  int value() const noexcept;
};

// A narrower interface than IntLike, so we can
// test narrowing constructors.
struct Blank {};

using TestAlloc = xyz::TrackingAllocator<std::byte>;
using TestProtocol = xyz::protocol<IntLike, TestAlloc>;

// A simple test class that we will place in protocol.
class Tester {
  int val_;

  // protocol should probably implement small buffer optimization, and therefore
  // we may find there are fewer allocations than these tests suggest. To
  // sidestep this, we can use a type that is guaranteed to be larger than
  // protocol.
  [[maybe_unused]] std::array<std::byte, sizeof(TestProtocol)> padding_;

 public:
  Tester(int value) noexcept : val_(value) {}

  // Just so we can exercise protocol's in-place init list constructor.
  Tester(std::initializer_list<int>) noexcept {}

  int value() const noexcept { return val_; }
};

TEST(ProtocolTest, Construction) {
  static_assert(
      not std::is_nothrow_constructible_v<TestProtocol, std::allocator_arg_t,
                                          TestAlloc, Tester>);

  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc{&allocs, &deallocs};
    TestProtocol p{std::allocator_arg, alloc, Tester{15}};

    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  // Memory is cleaned up after the block scope closes.
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

// TEST(ProtocolTest, InPlaceConstruction) {
//   unsigned allocs = 0;
//   unsigned deallocs = 0;
//   {
//     TestAlloc alloc{&allocs, &deallocs};
//     TestProtocol p{std::allocator_arg, alloc, std::in_place_type<Tester>, 15};

//     EXPECT_EQ(allocs, 1);
//     EXPECT_EQ(deallocs, 0);
//   }
//   EXPECT_EQ(allocs, 1);
//   EXPECT_EQ(deallocs, 1);
// }

// TEST(ProtocolTest, InitListConstruction) {
//   unsigned allocs = 0;
//   unsigned deallocs = 0;
//   {
//     TestAlloc alloc{&allocs, &deallocs};
//     TestProtocol p{
//         std::allocator_arg, alloc, std::in_place_type<Tester>, {1, 2, 3}};

//     EXPECT_EQ(allocs, 1);
//     EXPECT_EQ(deallocs, 0);
//   }
//   EXPECT_EQ(allocs, 1);
//   EXPECT_EQ(deallocs, 1);
// }

TEST(ProtocolTest, CopyConstruction) {
  static_assert(not std::is_nothrow_copy_constructible_v<TestProtocol>);

  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc, Tester{25}};
    xyz::protocol p2{p1};
    EXPECT_EQ(allocs, 2);  // Once for p1, and once for p2.
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, CopyConstructionEqual) {
  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    // TestAllocs are considered equivalent if they point to
    // the same allocs and deallocs variables. Therefore,
    // alloc1 == alloc2 here.
    TestAlloc alloc1{&allocs, &deallocs};
    TestAlloc alloc2{&allocs, &deallocs};
    ASSERT_EQ(alloc1, alloc2);

    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestProtocol p2{std::allocator_arg, alloc2, p1};

    // Both alloc1 and alloc2 point at the same counter.
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, CopyConstructionUnequal) {
  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;
  {
    // alloc1 != alloc2 now, so p1 and p2 should allocate with
    // their own respective allocators.
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestAlloc alloc2{&allocs2, &deallocs2};
    ASSERT_NE(alloc1, alloc2);

    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};
    // p2 copies from p1, but allocates using alloc2.
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs1, 1);  // From p1.
    EXPECT_EQ(allocs2, 1);  // From p2.
    EXPECT_EQ(deallocs1, 0);
    EXPECT_EQ(deallocs2, 0);
  }
  EXPECT_EQ(deallocs1, 1);
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, CopyAssignmentEqual) {
  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{30}};
    TestProtocol p2{std::allocator_arg, alloc, Tester{40}};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    p1 = p2;
    // One allocation for constructing p1; one for p2; and
    // one for copy assignment.
    EXPECT_EQ(allocs, 3);
    // p1's original state should have been deallocated.
    EXPECT_EQ(deallocs, 1);
  }
  EXPECT_EQ(allocs, 3);
  EXPECT_EQ(deallocs, 3);
}

TEST(ProtocolTest, CopyAssignmentUnequal) {
  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;
  {
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestAlloc alloc2{&allocs2, &deallocs2};
    ASSERT_NE(alloc1, alloc2);

    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};
    TestProtocol p2{std::allocator_arg, alloc2, Tester{200}};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    p1 = p2;

    // The new copy is created with alloc1, and the original
    // p1 is deallocated with alloc1.
    EXPECT_EQ(allocs1, 2);
    EXPECT_EQ(deallocs1, 1);

    // alloc2 was never used for additional memory.
    EXPECT_EQ(allocs2, 1);
  }
  EXPECT_EQ(deallocs1, 2);
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, MoveConstruction) {
  // protocol should always be nothrow move constructible.
  static_assert(std::is_nothrow_move_constructible_v<TestProtocol>);

  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{50}};
    EXPECT_EQ(allocs, 1);

    TestProtocol p2{std::move(p1)};
    EXPECT_EQ(allocs, 1);  // No new allocations.
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, MoveConstructionEqual) {
  static_assert(
      not std::is_nothrow_constructible_v<TestProtocol, std::allocator_arg_t,
                                          TestAlloc, TestProtocol&&>);

  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc1{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs, &deallocs};
    TestProtocol p2{std::allocator_arg, alloc2, std::move(p1)};

    // alloc1 == alloc2, so no new memory needs to be allocated by
    // p2.
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 0);
  }
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

TEST(ProtocolTest, MoveConstructionUnequal) {
  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;
  {
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};
    EXPECT_EQ(allocs1, 1);

    // alloc2 != alloc1. We cannot perform a simple pointer-swap
    // in this case, and must reallocate and move the underlying
    // memory.
    TestAlloc alloc2{&allocs2, &deallocs2};
    TestProtocol p2{std::allocator_arg, alloc2, std::move(p1)};
    // A new allocation is performed by alloc2.
    EXPECT_EQ(allocs2, 1);
    // p1 is moved-from and therefore has deallocated its memory.
    EXPECT_EQ(deallocs1, 1);

    EXPECT_EQ(deallocs2, 0);
  }
  EXPECT_EQ(deallocs2, 1);
}

TEST(ProtocolTest, MoveAssignmentEqual) {
  // Since TestAllocs are not always equivalent and do not propagate on move,
  // operator=(TestProtocol&&) should not be noexcept.
  static_assert(not std::is_nothrow_move_assignable_v<TestProtocol>);

  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc{&allocs, &deallocs};

    TestProtocol p1{std::allocator_arg, alloc, Tester{30}};
    TestProtocol p2{std::allocator_arg, alloc, Tester{40}};
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    // Both use the same allocator, so moving is a simple pointer swap.
    p1 = std::move(p2);
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 1);  // p2 has been deallocated.
  }
  EXPECT_EQ(allocs, 2);
  EXPECT_EQ(deallocs, 2);
}

TEST(ProtocolTest, MoveAssignmentUnequal) {
  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;
  {
    TestAlloc alloc1{&allocs1, &deallocs1};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs2, &deallocs2};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    // alloc1 != alloc2, so the underlying type must be moved into
    // a new storage.
    p1 = std::move(p2);

    // Allocates for original construction, and once for move assignment.
    EXPECT_EQ(allocs1, 2);
    EXPECT_EQ(deallocs2, 1);
  }
  EXPECT_EQ(deallocs1, 2);
}

TEST(ProtocolTest, SwapEqual) {
  unsigned allocs = 0;
  unsigned deallocs = 0;
  {
    TestAlloc alloc1{&allocs, &deallocs};
    TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

    TestAlloc alloc2{&allocs, &deallocs};
    TestProtocol p2{std::allocator_arg, alloc2, p1};

    EXPECT_EQ(allocs, 2);

    // alloc1 == alloc2, so p1 and p2 should swap safely.
    using std::swap;
    swap(p1, p2);

    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 0);

    // Both keep their original allocator, but now manage
    // each other's memory.
    EXPECT_EQ(p1.get_allocator(), alloc1);
    EXPECT_EQ(p2.get_allocator(), alloc2);
  }
  EXPECT_EQ(deallocs, 2);
}

// Tests that swap fails gracefully with an assert in debug mode.
// In release mode, this is undefined behavior.
#if (defined(_MSC_VER) && defined(_DEBUG)) || (!defined(NDEBUG))
TEST(ProtocolTest, SwapUnequal) {
  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;
  TestAlloc alloc1{&allocs1, &deallocs1};
  TestProtocol p1{std::allocator_arg, alloc1, Tester{100}};

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;
  TestAlloc alloc2{&allocs2, &deallocs2};
  TestProtocol p2{std::allocator_arg, alloc2, p1};

  using std::swap;
  EXPECT_DEATH(swap(p1, p2),
               "allocators must compare equal or propagate on swap");
}
#endif

// TEST(ProtocolTest, NarrowingCopyConstruction) {
//   unsigned allocs = 0;
//   unsigned deallocs = 0;
//   {
//     TestAlloc alloc1{&allocs, &deallocs};
//     xyz::protocol<IntLike, TestAlloc> p1{std::allocator_arg, alloc1,
//                                          Tester{25}};
//     // Blank is a subset of IntLike.
//     xyz::protocol<Blank, TestAlloc> p2{p1};
//     // This is a deep copy still, so space must be allocated once for
//     // p1 and once for p2.
//     EXPECT_EQ(allocs, 2);
//     EXPECT_EQ(deallocs, 0);
//   }
//   EXPECT_EQ(deallocs, 2);
// }

// TEST(ProtocolTest, NarrowingCopyConstructionUnequal) {
//   unsigned allocs1 = 0;
//   unsigned deallocs1 = 0;

//   unsigned allocs2 = 0;
//   unsigned deallocs2 = 0;

//   {
//     TestAlloc alloc1{&allocs1, &deallocs1};
//     TestAlloc alloc2{&allocs2, &deallocs2};

//     xyz::protocol<IntLike, TestAlloc> p1{std::allocator_arg, alloc1,
//                                          Tester{25}};
//     xyz::protocol<Blank, TestAlloc> p2{std::allocator_arg, alloc2, p1};
//     EXPECT_EQ(allocs1, 1);  // Allocated p1.
//     EXPECT_EQ(allocs2, 1);  // Allocated p2.
//   }
//   EXPECT_EQ(deallocs1, 1);
//   EXPECT_EQ(deallocs2, 1);
// }

// TEST(ProtocolTest, NarrowingMoveConstruction) {
//   // Narrowing move construction should always be noexcept.
//   static_assert(
//       std::is_nothrow_constructible<xyz::protocol<IntLike, TestAlloc>,
//                                     xyz::protocol<Blank, TestAlloc>&&>);

//   unsigned allocs = 0;
//   unsigned deallocs = 0;
//   {
//     TestAlloc alloc1{&allocs, &deallocs};
//     xyz::protocol<IntLike, TestAlloc> p1{std::allocator_arg, alloc1,
//                                          Tester{25}};
//     xyz::protocol<Blank, TestAlloc> p2{std::move(p1)};
//     // Even though the interfaces are different, the allocators are the
//     // same. Move construction is a pointer swap.
//     EXPECT_EQ(allocs, 1);
//     EXPECT_EQ(deallocs, 0);
//   }
//   EXPECT_EQ(deallocs, 1);
// }

// TEST(ProtocolTest, NarrowingMoveConstructionEqual) {
//   // TestAllocs are not always equal, so allocator-aware narrowing move
//   // construction can potentially throw.
//   static_assert(
//       not std::is_nothrow_constructible<xyz::protocol<IntLike, TestAlloc>,
//                                         std::allocator_arg_t, TestAlloc,
//                                         xyz::protocol<Blank, TestAlloc>&&>);

//   unsigned allocs = 0;
//   unsigned deallocs = 0;

//   {
//     TestAlloc alloc1{&allocs, &deallocs};
//     TestAlloc alloc2{&allocs, &deallocs};

//     xyz::protocol<IntLike, TestAlloc> p1{std::allocator_arg, alloc1,
//                                          Tester{25}};
//     xyz::protocol<Blank, TestAlloc> p2{std::allocator_arg, alloc2,
//                                        std::move(p1)};
//     // Even though we explicitly provide an allocator, they both
//     // compare equal, so move construction is still a pointer swap.
//     EXPECT_EQ(allocs, 1);
//   }
//   EXPECT_EQ(deallocs, 1);
// }

// TEST(ProtocolTest, NarrowingMoveConstructionUnequal) {
//   unsigned allocs1 = 0;
//   unsigned deallocs1 = 0;

//   unsigned allocs2 = 0;
//   unsigned deallocs2 = 0;

//   {
//     TestAlloc alloc1{&allocs1, &deallocs1};
//     TestAlloc alloc2{&allocs2, &deallocs2};

//     xyz::protocol<IntLike, TestAlloc> p1{std::allocator_arg, alloc1,
//                                          Tester{25}};
//     xyz::protocol<Blank, TestAlloc> p2{std::allocator_arg, alloc2,
//                                        std::move(p1)};
//     // alloc1 != alloc2, so we must perform a new allocation for p2
//     // and move the underlying data.
//     EXPECT_EQ(allocs1, 1);
//     EXPECT_EQ(allocs2, 1);
//   }
//   EXPECT_EQ(deallocs1, 1);
//   EXPECT_EQ(deallocs2, 1);
// }

// Extends TrackingAllocator with POCCA set to true.
template <typename T>
struct PoccaAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_copy_assignment = std::true_type;

  template <typename Other>
  struct rebind {
    using other = PoccaAllocator<Other>;
  };
};

TEST(ProtocolTest, CopyAssignmentUnequalPocca) {
  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;

  {
    PoccaAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<IntLike, PoccaAllocator<std::byte>> p1{std::allocator_arg,
                                                         alloc1, Tester{0}};

    PoccaAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<IntLike, PoccaAllocator<std::byte>> p2{std::allocator_arg,
                                                         alloc2, Tester{0}};

    // p1 and p2 are both constructed with different allocators.
    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    // POCCA is true: p1 should now take p2's allocator instead of keeping its
    // own.
    p1 = p2;
    EXPECT_EQ(p1.get_allocator(), alloc2);

    EXPECT_EQ(allocs1, 1);  // alloc1 only allocated once for p1.
    // alloc2 allocated once for p2, and again when copying into p1.
    EXPECT_EQ(allocs2, 2);

    // p1 was deallocated when it got assigned to.
    EXPECT_EQ(deallocs1, 1);
    EXPECT_EQ(deallocs2, 0);
  }
  // p2 and p1 have both been deallocated with alloc2.
  EXPECT_EQ(deallocs2, 2);
}

// Extends TrackingAllocator with POCMA set to true.
template <typename T>
struct PocmaAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_move_assignment = std::true_type;

  template <typename Other>
  struct rebind {
    using other = PocmaAllocator<Other>;
  };
};

TEST(ProtocolTest, MoveAssignmentUnequalPocma) {
  // Now that POCMA is true, move assignment should be noexcept.
  static_assert(std::is_nothrow_move_assignable_v<
                xyz::protocol<IntLike, PocmaAllocator<std::byte>>>);

  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;

  {
    PocmaAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<IntLike, PocmaAllocator<std::byte>> p1{std::allocator_arg,
                                                         alloc1, Tester{0}};

    PocmaAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<IntLike, PocmaAllocator<std::byte>> p2{std::allocator_arg,
                                                         alloc2, Tester{10}};

    // p1 and p2 are both constructed with different allocators.
    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    // POCMA is true: p1 takes p2's allocator. Even though alloc1 != alloc2,
    // p1 can just steal p2's pointer now, because it will have alloc2
    // henceforth.
    p1 = std::move(p2);
    EXPECT_EQ(allocs1, 1);    // Still only the one original allocation.
    EXPECT_EQ(deallocs1, 1);  // p1's original state was deallocated.

    // No new allocations since it was just a pointer swap.
    EXPECT_EQ(allocs2, 1);
    EXPECT_EQ(deallocs2, 0);
  }

  EXPECT_EQ(deallocs2, 1);
}

// Extends TrackingAllocator with POCS set to true.
template <typename T>
struct PocsAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_swap = std::true_type;

  template <typename Other>
  struct rebind {
    using other = PocsAllocator<Other>;
  };
};

TEST(ProtocolTest, SwapUnequalPocs) {
  // Even for unequal allocators, swapping should be noexcept when POCS is true.
  static_assert(std::is_nothrow_swappable_v<
                xyz::protocol<IntLike, PocsAllocator<std::byte>>>);

  unsigned allocs1 = 0;
  unsigned deallocs1 = 0;

  unsigned allocs2 = 0;
  unsigned deallocs2 = 0;
  {
    PocsAllocator<std::byte> alloc1{&allocs1, &deallocs1};
    xyz::protocol<IntLike, PocsAllocator<std::byte>> p1{std::allocator_arg,
                                                        alloc1, Tester{0}};

    PocsAllocator<std::byte> alloc2{&allocs2, &deallocs2};
    xyz::protocol<IntLike, PocsAllocator<std::byte>> p2{std::allocator_arg,
                                                        alloc2, Tester{10}};

    // p1 and p2 were constructed with different allocators.
    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);

    // p1 and p2 now swap both their underlying types AND their allocators.
    using std::swap;
    swap(p1, p2);

    // Allocators are now swapped.
    EXPECT_EQ(p1.get_allocator(), alloc2);
    EXPECT_EQ(p2.get_allocator(), alloc1);

    // No new allocations were performed.
    EXPECT_EQ(allocs1, 1);
    EXPECT_EQ(allocs2, 1);
  }

  EXPECT_EQ(deallocs1, 1);
  EXPECT_EQ(deallocs2, 1);
}

// A simple exception to test catching.
struct TestException : std::bad_alloc {};

// An extension of tracking allocator that holds a pointer to
// a flag indicating whether or not it should throw upon allocation.
// Tests can pass in should_throw and change its value to exercise exception
// safety.
template <typename T>
struct ThrowingAllocator : xyz::TrackingAllocator<T> {
  const bool* should_throw_;

  template <typename Other>
  struct rebind {
    using other = ThrowingAllocator<Other>;
  };

  ThrowingAllocator(unsigned* allocs, unsigned* deallocs,
                    const bool* should_throw)
      : xyz::TrackingAllocator<T>(allocs, deallocs), should_throw_(should_throw) {}

  T* allocate(std::size_t count) {
    if (*should_throw_) {
      throw TestException{};
    }
    return xyz::TrackingAllocator<T>::allocate(count);
  }

  template <typename U>
  ThrowingAllocator(const ThrowingAllocator<U>& other)
    : xyz::TrackingAllocator<T>(other), should_throw_(other.should_throw_) {}
};

using ThrowingAlloc = ThrowingAllocator<std::byte>;

using ThrowingProtocol = xyz::protocol<IntLike, ThrowingAlloc>;

TEST(ProtocolTest, ConstructionException) {
  unsigned allocs = 0;
  unsigned deallocs = 0;
  bool should_throw{true};

  ThrowingAlloc alloc{&allocs, &deallocs, &should_throw};
  EXPECT_THROW((ThrowingProtocol{std::allocator_arg, alloc, Tester{0}}),
               TestException);
  // alloc threw before any allocations were performed.
  EXPECT_EQ(allocs, 0);
  EXPECT_EQ(deallocs, 0);
}

// TEST(ProtocolTest, CopyConstructionException) {
//   unsigned allocs = 0;
//   unsigned deallocs = 0;

//   {
//     bool should_throw{false};

//     ThrowingAlloc alloc{&allocs, &deallocs, &should_throw};
//     ThrowingProtocol p1{std::allocator_arg, alloc, Tester{10}};
//     // p1 was allocated safely.
//     EXPECT_EQ(allocs, 1);

//     should_throw = true;
//     // We attempt to copy p1, and now throw during allocation.
//     EXPECT_THROW(ThrowingProtocol{p1}, TestException);
//     // p1 should be unchanged.
//     EXPECT_EQ(p1.value(), 10);
//   }

//   EXPECT_EQ(deallocs, 1);
// }

// TEST(ProtocolTest, CopyAssignmentException) {
//   unsigned allocs = 0;
//   unsigned deallocs = 0;

//   {
//     bool should_throw{false};

//     // p1 and p2 are both constructed without issue.
//     ThrowingAlloc alloc{&allocs, &deallocs, &should_throw};
//     ThrowingProtocol p1{std::allocator_arg, alloc, Tester{10}};
//     ThrowingProtocol p2{std::allocator_arg, alloc, Tester{20}};

//     EXPECT_EQ(allocs, 2);
//     EXPECT_EQ(deallocs, 0);

//     // Copy assignment triggers an allocation and throws an exception.
//     should_throw = true;
//     EXPECT_THROW(p2 = p1, TestException);

//     // Both values are left in their original states. This is the strong
//     // exception guarantee.
//     EXPECT_EQ(p1.value(), 10);
//     EXPECT_EQ(p2.value(), 20);
//   }
//   EXPECT_EQ(deallocs, 2);
// }

TEST(ProtocolTest, EqualMoveConstructionNoException) {
  unsigned allocs = 0;
  unsigned deallocs = 0;

  bool should_throw{false};

  ThrowingAlloc alloc{&allocs, &deallocs, &should_throw};
  ThrowingProtocol p1{std::allocator_arg, alloc, Tester{15}};
  EXPECT_EQ(allocs, 1);

  should_throw = true;
  // Even though allocation will throw, moving from p1 should
  // not require any allocation and therefore not throw.
  EXPECT_NO_THROW(ThrowingProtocol{std::move(p1)});
  EXPECT_EQ(allocs, 1);
  EXPECT_EQ(deallocs, 1);
}

// TEST(ProtocolTest, UnequalMoveConstructionException) {
//   unsigned allocs1 = 0;
//   unsigned deallocs1 = 0;

//   unsigned allocs2 = 0;
//   unsigned deallocs2 = 0;

//   {
//     bool should_throw{false};

//     ThrowingAlloc alloc1{&allocs1, &deallocs1, &should_throw};
//     ThrowingProtocol p1{std::allocator_arg, alloc1, Tester{25}};

//     should_throw = true;
//     ThrowingAlloc alloc2{&allocs2, &deallocs2, &should_throw};

//     // We now construct with an alloc2, which is not equal to alloc1. This
//     // requires us to allocate space and then move from p1, which triggers an
//     // exception.
//     EXPECT_THROW((ThrowingProtocol{std::allocator_arg, alloc2, std::move(p1)}),
//                  TestException);

//     EXPECT_EQ(allocs1, 1);
//     EXPECT_EQ(deallocs1, 0);
//     EXPECT_EQ(allocs2, 0);

//     // p1 should be unmodified.
//     ASSERT_FALSE(p1.valueless_after_move());
//     EXPECT_EQ(p1.value(), 25);
//   }

//   EXPECT_EQ(deallocs1, 1);
// }

TEST(ProtocolTest, EqualMoveAssignmentNoException) {
  unsigned allocs = 0;
  unsigned deallocs = 0;

  {
    bool should_throw{false};

    ThrowingAlloc alloc{&allocs, &deallocs, &should_throw};
    ThrowingProtocol p1{std::allocator_arg, alloc, Tester{15}};
    ThrowingProtocol p2{std::allocator_arg, alloc, Tester{35}};
    EXPECT_EQ(allocs, 2);

    // When p1 and p2 have equal allocators, move assignment should
    // not throw.
    should_throw = true;
    p1 = std::move(p2);
    EXPECT_EQ(allocs, 2);
    EXPECT_EQ(deallocs, 1);
  }

  EXPECT_EQ(deallocs, 2);
}

// TEST(ProtocolTest, UnequalMoveAssignmentException) {
//   unsigned allocs1 = 0;
//   unsigned deallocs1 = 0;

//   unsigned allocs2 = 0;
//   unsigned deallocs2 = 0;

//   {
//     bool should_throw1{false};
//     bool should_throw2{false};

//     ThrowingAlloc alloc1{&allocs1, &deallocs1, &should_throw1};
//     ThrowingProtocol p1{std::allocator_arg, alloc1, Tester{25}};

//     ThrowingAlloc alloc2{&allocs2, &deallocs2, &should_throw2};
//     ThrowingProtocol p2{std::allocator_arg, alloc2, Tester{55}};

//     EXPECT_EQ(allocs1, 1);
//     EXPECT_EQ(allocs2, 1);

//     // Move assignment with unequal allocators requires new allocation.
//     // This results in an exception being thrown.
//     should_throw1 = true;
//     EXPECT_THROW(p1 = std::move(p2), TestException);
//     EXPECT_EQ(deallocs2, 0);

//     // p1 should be valid after the exception is caught.
//     EXPECT_EQ(p1.value(), 25);

//     // p2 should not have been destroyed.
//     EXPECT_FALSE(p2.valueless_after_move());
//     EXPECT_EQ(p2.value(), 55);
//   }

//   EXPECT_EQ(deallocs1, 1);
//   EXPECT_EQ(deallocs2, 1);
// }

}  // namespace

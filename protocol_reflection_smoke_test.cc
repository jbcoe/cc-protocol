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
// Exercises real xyz::protocol dispatch on a throwaway single-const-method
// interface, keeping this file's compile/link surface minimal.
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "interface_A.h"
#include "interface_A_Subset.h"
#include "interface_B.h"
#include "interface_C.h"
#include "protocol.h"
#include "tracking_allocator.h"

namespace {

struct Greeter {
  std::string_view name() const;
};

struct GreeterImpl {
  std::string value;

  std::string_view name() const { return value; }
};

struct NotAGreeter {
  int compute() const;
};

TEST(ProtocolReflectionSmoke, DispatchesToTheRealImplementation) {
  xyz::protocol<Greeter> p(std::in_place_type<GreeterImpl>, "hello");
  EXPECT_EQ(p.name(), "hello");
}

TEST(ProtocolReflectionSmoke, CopyIsIndependentAndDispatchesTheSame) {
  xyz::protocol<Greeter> p(std::in_place_type<GreeterImpl>, "hello");
  xyz::protocol<Greeter> copy(p);
  EXPECT_EQ(copy.name(), "hello");
}

TEST(ProtocolReflectionSmoke, MoveLeavesTheSourceValuelessAndTargetWorking) {
  xyz::protocol<Greeter> p(std::in_place_type<GreeterImpl>, "hello");
  xyz::protocol<Greeter> moved(std::move(p));
  EXPECT_EQ(moved.name(), "hello");
  EXPECT_TRUE(p.valueless_after_move());
}

TEST(ProtocolReflectionSmoke, NonConformingTypeFailsToCompile) {
  static_assert(!std::is_constructible_v<xyz::protocol<Greeter>,
                                         std::in_place_type_t<NotAGreeter>>);
}

// name() is const and noexcept; count() is not. The vtable already erases
// every entry through void* uniformly regardless of member constness, so
// this proves mixed const/non-const dispatch works.
struct ALike {
  std::string name_ = "ALike";
  int count_ = 0;

  ALike() = default;

  explicit ALike(std::string_view name) : name_(name) {}

  std::string_view name() const noexcept { return name_; }

  int count() { return ++count_; }
};

TEST(ProtocolReflectionSmoke, DispatchesBothConstAndNonConstMembersOfA) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>);
  EXPECT_EQ(a.name(), "ALike");
  EXPECT_EQ(a.count(), 1);
  EXPECT_EQ(a.count(), 2);
}

TEST(ProtocolReflectionSmoke, ANameIsActuallyNoexcept) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>);
  static_assert(noexcept(a.name()));
}

// Interface C's compute() overloads share one name but need one distinct
// vtable entry each and one merged forwarder: proves the overload grouping
// in protocol_reflection.hxx (protocol_member_wrapper_combinator) actually
// resolves through real duck-typed dispatch, not just that the concept in
// conformance_test.cc accepts it.
struct CLike {
  int compute(int x) { return x * 2; }

  double compute(double x) { return x * 3.0; }

  std::string compute(const std::string& x) const { return x + x; }
};

TEST(ProtocolReflectionSmoke, DispatchesEachOverloadOfCToTheMatchingCandidate) {
  xyz::protocol<xyz::C> c(std::in_place_type<CLike>);
  EXPECT_EQ(c.compute(5), 10);
  EXPECT_EQ(c.compute(2.0), 6.0);
  EXPECT_EQ(c.compute(std::string("ab")), "abab");
}

// Interface B: plain, non-overloaded members. No new protocol_reflection.hxx
// machinery.
struct BLike {
  std::vector<int> results_;
  bool ready_ = false;

  void process(const std::string& input) {
    results_.push_back(static_cast<int>(input.length()));
    ready_ = true;
  }

  std::vector<int> get_results() const { return results_; }

  bool is_ready() const { return ready_; }
};

TEST(ProtocolReflectionSmoke, DispatchesAllThreeMembersOfB) {
  xyz::protocol<xyz::B> b(std::in_place_type<BLike>);
  EXPECT_FALSE(b.is_ready());
  b.process("hello");
  EXPECT_TRUE(b.is_ready());
  EXPECT_EQ(b.get_results(), (std::vector<int>{5}));
}

// Allocator-awareness: standalone equivalents of protocol_test.cc's own
// TrackingAllocator-based tests, proving the allocator-extended
// constructors, select_on_container_copy_construction, and the
// equal-vs-non-equal-allocator move/swap paths all really run through
// TrackingAllocator rather than silently falling back to Allocator{}.

TEST(ProtocolReflectionSmoke, CountAllocationsForInPlaceConstruction) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    EXPECT_EQ(alloc_counter, 1u);
    EXPECT_EQ(dealloc_counter, 0u);
  }
  EXPECT_EQ(alloc_counter, 1u);
  EXPECT_EQ(dealloc_counter, 1u);
}

TEST(ProtocolReflectionSmoke, CountAllocationsForCopyConstruction) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(a);
    EXPECT_EQ(alloc_counter, 2u);
  }
  EXPECT_EQ(dealloc_counter, 2u);
}

TEST(ProtocolReflectionSmoke, CountAllocationsForMoveConstruction) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(std::move(a));
  }
  EXPECT_EQ(alloc_counter, 1u);
  EXPECT_EQ(dealloc_counter, 1u);
}

TEST(ProtocolReflectionSmoke, CountAllocationsForMoveAssignment) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    aa = std::move(a);
  }
  EXPECT_EQ(alloc_counter, 2u);
  EXPECT_EQ(dealloc_counter, 2u);
}

template <typename T>
struct NonEqualTrackingAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_move_assignment = std::true_type;

  template <typename Other>
  struct rebind {
    using other = NonEqualTrackingAllocator<Other>;
  };

  friend bool operator==(const NonEqualTrackingAllocator&,
                         const NonEqualTrackingAllocator&) noexcept {
    return false;
  }

  friend bool operator!=(const NonEqualTrackingAllocator&,
                         const NonEqualTrackingAllocator&) noexcept {
    return true;
  }
};

TEST(ProtocolReflectionSmoke,
     CountAllocationsForMoveAssignmentWhenAllocatorsDontCompareEqual) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> a(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> aa(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    EXPECT_EQ(alloc_counter, 2u);
    aa = std::move(a);  // Copies (move-constructs into new storage): the
                        // allocators never compare equal, so the cheap
                        // pointer-steal path isn't available.
  }
  EXPECT_EQ(alloc_counter, 3u);
  EXPECT_EQ(dealloc_counter, 3u);
}

template <typename T>
struct POCSTrackingAllocator : xyz::TrackingAllocator<T> {
  using xyz::TrackingAllocator<T>::TrackingAllocator;
  using propagate_on_container_swap = std::true_type;

  template <typename Other>
  struct rebind {
    using other = POCSTrackingAllocator<Other>;
  };
};

TEST(ProtocolReflectionSmoke, MemberSwapWhenAllocatorsDontCompareEqual) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  xyz::protocol<xyz::A, POCSTrackingAllocator<std::byte>> p(
      std::allocator_arg,
      POCSTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
      std::in_place_type<ALike>);
  xyz::protocol<xyz::A, POCSTrackingAllocator<std::byte>> pp(
      std::allocator_arg,
      POCSTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
      std::in_place_type<ALike>, "pp");
  p.swap(pp);
  EXPECT_EQ(p.name(), "pp");
  EXPECT_EQ(pp.name(), "ALike");
}

TEST(ProtocolReflectionSmoke, AllocatorExtendedCopyFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::allocator_arg, std::allocator<std::byte>(), p);
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolReflectionSmoke, AllocatorExtendedMoveFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::allocator_arg, std::allocator<std::byte>(),
                            std::move(p));
  EXPECT_TRUE(ppp.valueless_after_move());
}

// Narrowing conversions: A (name, count) -> A_Subset (name only), the
// machinery wired into protocol_reflection.hxx's converting constructors
// and get_owning_vtable/get_mapped_vtable (protocol.cc, backend-agnostic).

TEST(ProtocolReflectionSmoke, NarrowingCopyConversion) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(
      std::allocator_arg, std::allocator<std::byte>(),
      std::in_place_type<ALike>);
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset(p);
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_FALSE(p_subset.valueless_after_move());
  EXPECT_EQ(p.name(), "ALike");
  EXPECT_EQ(p_subset.name(), "ALike");
}

TEST(ProtocolReflectionSmoke, NarrowingMoveConversionEqualAllocators) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(
      std::allocator_arg, std::allocator<std::byte>(),
      std::in_place_type<ALike>);
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
      std::move(p);
  EXPECT_TRUE(p.valueless_after_move());
  EXPECT_FALSE(p_subset.valueless_after_move());
  EXPECT_EQ(p_subset.name(), "ALike");
}

TEST(ProtocolReflectionSmoke, NarrowingCopyConversionFromValueless) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(
      std::allocator_arg, std::allocator<std::byte>(),
      std::in_place_type<ALike>);
  xyz::protocol<xyz::A, std::allocator<std::byte>> p2 = std::move(p);
  EXPECT_TRUE(p.valueless_after_move());
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset(p);
  EXPECT_TRUE(p_subset.valueless_after_move());
}

TEST(ProtocolReflectionSmoke, CountAllocationsForNarrowingCopyConversion) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> p(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    EXPECT_EQ(alloc_counter, 1u);
    xyz::protocol<xyz::A_Subset, xyz::TrackingAllocator<std::byte>> p_subset(p);
    EXPECT_EQ(alloc_counter, 2u);
    EXPECT_EQ(dealloc_counter, 0u);
  }
  EXPECT_EQ(alloc_counter, 2u);
  EXPECT_EQ(dealloc_counter, 2u);
}

TEST(ProtocolReflectionSmoke, CountAllocationsForNarrowingMoveConversion) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    // Equal allocators: the pointer is stolen, no new allocation.
    xyz::protocol<xyz::A_Subset, xyz::TrackingAllocator<std::byte>> aa(
        std::move(a));
    EXPECT_TRUE(a.valueless_after_move());
    EXPECT_FALSE(aa.valueless_after_move());
    EXPECT_EQ(alloc_counter, 1u);
    EXPECT_EQ(dealloc_counter, 0u);
  }
  EXPECT_EQ(alloc_counter, 1u);
  EXPECT_EQ(dealloc_counter, 1u);
}

TEST(ProtocolReflectionSmoke, NarrowingCopyConversionNonEqualAllocators) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    EXPECT_EQ(alloc_counter, 1u);
    NonEqualTrackingAllocator<std::byte> target_alloc(&alloc_counter,
                                                      &dealloc_counter);
    xyz::protocol<xyz::A_Subset, NonEqualTrackingAllocator<std::byte>> p_subset(
        std::allocator_arg, target_alloc, p);
    EXPECT_FALSE(p.valueless_after_move());
    EXPECT_FALSE(p_subset.valueless_after_move());
    EXPECT_EQ(p_subset.name(), "ALike");
    // 1 allocation for the source, 1 more for the cloned target.
    EXPECT_EQ(alloc_counter, 2u);
    EXPECT_EQ(dealloc_counter, 0u);
  }
  EXPECT_EQ(alloc_counter, 2u);
  EXPECT_EQ(dealloc_counter, 2u);
}

TEST(ProtocolReflectionSmoke, NarrowingMoveConversionNonEqualAllocators) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>);
    EXPECT_EQ(alloc_counter, 1u);
    // Non-equal allocators force move-construction into new storage on the
    // target's allocator, and the source is destroyed immediately.
    xyz::protocol<xyz::A_Subset, NonEqualTrackingAllocator<std::byte>> p_subset(
        std::move(p));
    EXPECT_TRUE(p.valueless_after_move());
    EXPECT_FALSE(p_subset.valueless_after_move());
    EXPECT_EQ(p_subset.name(), "ALike");
    EXPECT_EQ(alloc_counter, 2u);
    EXPECT_EQ(dealloc_counter, 1u);
  }
  EXPECT_EQ(alloc_counter, 2u);
  EXPECT_EQ(dealloc_counter, 2u);
}

TEST(ProtocolReflectionSmoke, NarrowingMoveConversionFromValueless) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(
      std::allocator_arg, std::allocator<std::byte>(),
      std::in_place_type<ALike>);
  xyz::protocol<xyz::A, std::allocator<std::byte>> p2 = std::move(p);
  EXPECT_TRUE(p.valueless_after_move());
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
      std::move(p);
  EXPECT_TRUE(p_subset.valueless_after_move());
}

// Owning-conversion slice of protocol_test.cc's
// NarrowingConversionConcurrentStressing: exercises protocol.cc's
// get_mapped_vtable caching/locking under contention.
TEST(ProtocolReflectionSmoke, NarrowingConversionConcurrentStressing) {
  constexpr int kNumThreads = 20;
  constexpr int kIterationsPerThread = 50;
  std::vector<std::thread> threads;
  std::atomic<bool> start_signal{false};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&start_signal]() {
      while (!start_signal.load()) {
        std::this_thread::yield();
      }
      for (int iter = 0; iter < kIterationsPerThread; ++iter) {
        xyz::protocol<xyz::A, std::allocator<std::byte>> p(
            std::in_place_type<ALike>);
        xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
            std::move(p);
        EXPECT_EQ(p_subset.name(), "ALike");

        xyz::protocol<xyz::A, std::allocator<char>> p_char(
            std::in_place_type<ALike>);
        xyz::protocol<xyz::A_Subset, std::allocator<char>> p_subset_char =
            std::move(p_char);
        EXPECT_EQ(p_subset_char.name(), "ALike");
      }
    });
  }

  start_signal.store(true);
  for (auto& thread : threads) {
    thread.join();
  }
}

// xyz::protocol_view<T>/protocol_view<const T>: standalone equivalents of
// protocol_test.cc's own ProtocolViewTest suite.

TEST(ProtocolReflectionSmoke, ViewFromMutableConcrete) {
  ALike a("view_test");
  xyz::protocol_view<xyz::A> view(a);
  EXPECT_EQ(view.name(), "view_test");
  EXPECT_EQ(view.count(), 1);
  EXPECT_EQ(a.count_, 1);  // The view mutated `a` directly.
}

TEST(ProtocolReflectionSmoke, ViewFromMutableProtocol) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, "proto_view");
  xyz::protocol_view<xyz::A> view(a);
  EXPECT_EQ(view.name(), "proto_view");
  EXPECT_EQ(view.count(), 1);
  EXPECT_EQ(a.count(), 2);  // The view mutated `a` directly.
}

TEST(ProtocolReflectionSmoke, ConstViewFromMutableConcrete) {
  ALike a("view_test");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "view_test");
}

TEST(ProtocolReflectionSmoke, ConstViewFromMutableProtocol) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, "proto_view");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "proto_view");
}

TEST(ProtocolReflectionSmoke, ConstViewFromMutViewConcrete) {
  ALike a("view_test");
  xyz::protocol_view<xyz::A> mut_view(a);
  xyz::protocol_view<const xyz::A> const_view(mut_view);
  EXPECT_EQ(const_view.name(), "view_test");
}

TEST(ProtocolReflectionSmoke, ConstViewFromConstProtocol) {
  const xyz::protocol<xyz::A> a(std::in_place_type<ALike>, "proto_view");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "proto_view");
}

TEST(ProtocolReflectionSmoke, ViewConstnessRouting) {
  BLike b;
  xyz::protocol_view<xyz::B> view(b);
  EXPECT_FALSE(view.is_ready());
  view.process("view processing");
  EXPECT_TRUE(view.is_ready());
  auto results = view.get_results();
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0], 15);
}

class CopyCounter {
 public:
  int* copies_;

  explicit CopyCounter(int* copies) : copies_(copies) {}

  CopyCounter(const CopyCounter& other) : copies_(other.copies_) {
    if (copies_) (*copies_)++;
  }

  CopyCounter& operator=(const CopyCounter& other) {
    copies_ = other.copies_;
    if (copies_) (*copies_)++;
    return *this;
  }

  CopyCounter(CopyCounter&&) = default;
  CopyCounter& operator=(CopyCounter&&) = default;

  std::string_view name() const noexcept { return "CopyCounter"; }

  int count() { return 0; }
};

TEST(ProtocolReflectionSmoke, ViewCopiesAreShallow) {
  int copies = 0;
  CopyCounter c(&copies);
  xyz::protocol_view<xyz::A> view(c);
  EXPECT_EQ(copies, 0);

  xyz::protocol_view<xyz::A> view2 = view;
  EXPECT_EQ(copies, 0);

  xyz::protocol_view<xyz::A> view3(view2);
  EXPECT_EQ(view3.count(), 0);
  EXPECT_EQ(copies, 0);
}

TEST(ProtocolReflectionSmoke, ViewMoveIsStandard) {
  ALike a("move_test");
  xyz::protocol_view<xyz::A> view(a);
  xyz::protocol_view<xyz::A> view2 = std::move(view);

  // Moved-from view is still valid: it's just a pointer copy.
  EXPECT_EQ(view.name(), "move_test");
  EXPECT_EQ(view2.name(), "move_test");
}

TEST(ProtocolReflectionSmoke, PreventConstructionFromRValues) {
  static_assert(!std::constructible_from<xyz::protocol_view<xyz::A>, ALike>);
  static_assert(!std::constructible_from<xyz::protocol_view<xyz::A>,
                                         xyz::protocol<xyz::A>>);
  static_assert(
      !std::constructible_from<xyz::protocol_view<const xyz::A>, ALike>);
  static_assert(!std::constructible_from<xyz::protocol_view<const xyz::A>,
                                         xyz::protocol<xyz::A>>);
  static_assert(!std::constructible_from<xyz::protocol_view<const xyz::A>,
                                         const xyz::protocol<xyz::A>>);
  static_assert(!std::constructible_from<xyz::protocol_view<xyz::A_Subset>,
                                         xyz::protocol<xyz::A>>);
  static_assert(
      !std::constructible_from<xyz::protocol_view<const xyz::A_Subset>,
                               xyz::protocol<xyz::A>>);
}

// Narrowing conversions between views: A -> A_Subset, mirroring the owning
// narrowing tests above but through get_const_vtable/get_vtable instead of
// get_owning_vtable.

TEST(ProtocolReflectionSmoke, NarrowingViewConversions) {
  ALike a_obj;
  xyz::protocol_view<xyz::A> view_a(a_obj);

  xyz::protocol_view<const xyz::A_Subset> const_view_subset = view_a;
  EXPECT_EQ(const_view_subset.name(), "ALike");

  xyz::protocol_view<xyz::A_Subset> view_subset = view_a;
  EXPECT_EQ(view_subset.name(), "ALike");
}

TEST(ProtocolReflectionSmoke, ConstViewNarrowingFromConstView) {
  ALike a_obj;
  xyz::protocol_view<const xyz::A> const_a(a_obj);
  xyz::protocol_view<const xyz::A_Subset> const_subset(const_a);
  EXPECT_EQ(const_subset.name(), "ALike");
}

// The protocol_view counterpart to NarrowingConversionConcurrentStressing:
// proves get_mapped_vtable caching/locking under contention for
// protocol_view's own narrowing path too.
TEST(ProtocolReflectionSmoke, ViewNarrowingConversionConcurrentStressing) {
  constexpr int kNumThreads = 20;
  constexpr int kIterationsPerThread = 50;
  std::vector<std::thread> threads;
  std::atomic<bool> start_signal{false};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&start_signal]() {
      while (!start_signal.load()) {
        std::this_thread::yield();
      }
      for (int iter = 0; iter < kIterationsPerThread; ++iter) {
        ALike a_obj;
        xyz::protocol_view<xyz::A> view_a(a_obj);

        xyz::protocol_view<const xyz::A_Subset> const_view = view_a;
        EXPECT_EQ(const_view.name(), "ALike");

        xyz::protocol_view<xyz::A_Subset> mut_view = view_a;
        EXPECT_EQ(mut_view.name(), "ALike");
      }
    });
  }

  start_signal.store(true);
  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace

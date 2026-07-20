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

#include "protocol.h"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "interface_A.h"
#include "interface_A_Subset.h"
#include "interface_B.h"
#include "interface_C.h"
#include "interface_D.h"

// The reflection backend (enabled by protocol.h's own unconditional include
// of protocol_reflection.h) needs no per-interface generated header: it
// synthesizes the same machinery from the plain interface struct above at
// compile time. The Python backend still needs its generated headers.
#ifndef XYZ_PROTOCOL_ENABLE_REFLECTION
#include "generated/protocol_A.h"
#include "generated/protocol_A_Subset.h"
#include "generated/protocol_B.h"
#include "generated/protocol_C.h"
#include "generated/protocol_D.h"
#endif
#include "tracking_allocator.h"

namespace {

class ALike {
  int x_ = 42;
  std::string name_ = "ALike";

 public:
  ALike() = default;
  ALike(int x) : x_(x) {};
  ALike(std::string_view name) : name_(name) {};
  ALike(int x, std::string_view name) : x_(x), name_(name) {};

  std::string_view name() const noexcept { return name_; }

  int count() {
    int ret = x_;
    x_++;
    return ret;
  }
};

class ConstALike {
  std::string name_ = "ConstALike";

 public:
  ConstALike() = default;

  ConstALike(std::string_view name) : name_(name) {}

  std::string_view name() const noexcept { return name_; }

  // Notice: no count() method!
};

static_assert(noexcept(std::declval<xyz::protocol_view<xyz::A>>().name()));
static_assert(
    noexcept(std::declval<xyz::protocol_view<const xyz::A>>().name()));
static_assert(!noexcept(std::declval<xyz::protocol_view<xyz::A>>().count()));

static_assert(noexcept(std::declval<xyz::protocol<xyz::A>>().name()));
static_assert(!noexcept(std::declval<xyz::protocol<xyz::A>>().count()));

class BLike {
  std::vector<int> results_;
  bool ready_ = false;

 public:
  void process(const std::string& input) {
    results_.push_back(static_cast<int>(input.length()));
    ready_ = true;
  }

  std::vector<int> get_results() const { return results_; }

  bool is_ready() const { return ready_; }
};

class CLike {
 public:
  int compute(int x) { return x * 2; }

  double compute(double x) { return x * 3.0; }

  std::string compute(const std::string& x) const { return x + x; }
};

class CopyCounter {
 public:
  int* copies_;

  CopyCounter(int* copies) : copies_(copies) {}

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

TEST(ProtocolTest, InPlaceCtorNoArgs) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(ProtocolTest, InPlaceCtorSingleArg) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 42);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(ProtocolTest, InPlaceCtorMultipleArgs) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 180, "CustomName");
  EXPECT_EQ(a.name(), "CustomName");
  EXPECT_EQ(a.count(), 180);
}

TEST(ProtocolTest, MemberFunctions) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>);
  EXPECT_EQ(a.name(), "ALike");
  EXPECT_EQ(a.count(), 42);
}

TEST(ProtocolTest, CopyCtor) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 100, "Original");
  xyz::protocol<xyz::A> aa(a);
  EXPECT_EQ(aa.name(), "Original");
  EXPECT_EQ(aa.count(), 100);
  EXPECT_FALSE(a.valueless_after_move());
}

TEST(ProtocolTest, MoveCtor) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 100, "Original");
  xyz::protocol<xyz::A> aa(std::move(a));
  EXPECT_EQ(aa.name(), "Original");
  EXPECT_EQ(aa.count(), 100);
  EXPECT_TRUE(
      a.valueless_after_move());  // NOLINT(clang-analyzer-cplusplus.Move)
}

TEST(ProtocolTest, ProtocolBMemberFunctions) {
  xyz::protocol<xyz::B> b(std::in_place_type<BLike>);
  EXPECT_FALSE(b.is_ready());
  b.process("hello world");
  EXPECT_TRUE(b.is_ready());
  auto results = b.get_results();
  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(results[0], 11);
}

TEST(ProtocolTest, ProtocolBMultipleCalls) {
  xyz::protocol<xyz::B> b(std::in_place_type<BLike>);
  b.process("test1");
  b.process("test2_longer");

  auto results = b.get_results();
  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(results[0], 5);
  EXPECT_EQ(results[1], 12);
}

TEST(ProtocolTest, ProtocolCOverloads) {
  xyz::protocol<xyz::C> c(std::in_place_type<CLike>);

  EXPECT_EQ(c.compute(5), 10);
  EXPECT_EQ(c.compute(5.0), 15.0);

  const auto& const_c = c;
  EXPECT_EQ(const_c.compute(std::string("A")), "AA");
}

TEST(ProtocolTest, CountAllocationsForInPlaceConstruction) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);
  }
  EXPECT_EQ(alloc_counter, 1);
  EXPECT_EQ(dealloc_counter, 1);
}

TEST(ProtocolTest, CountAllocationsForCopyConstruction) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(a);
  }
  EXPECT_EQ(alloc_counter, 2);
  EXPECT_EQ(dealloc_counter, 2);
}

TEST(ProtocolTest, CountAllocationsForCopyAssignment) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 101);
    EXPECT_EQ(alloc_counter, 2);
    EXPECT_EQ(dealloc_counter, 0);
    aa = a;
  }
  EXPECT_EQ(alloc_counter, 3);
  EXPECT_EQ(dealloc_counter, 3);
}

TEST(ProtocolTest, CountAllocationsForMoveConstruction) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(std::move(a));
  }
  EXPECT_EQ(alloc_counter, 1);
  EXPECT_EQ(dealloc_counter, 1);
}

TEST(ProtocolTest, CountAllocationsForMoveAssignment) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> aa(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 101);
    EXPECT_EQ(alloc_counter, 2);
    EXPECT_EQ(dealloc_counter, 0);
    aa = std::move(a);
  }
  EXPECT_EQ(alloc_counter, 2);
  EXPECT_EQ(dealloc_counter, 2);
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

TEST(ProtocolTest,
     CountAllocationsForMoveAssignmentWhenAllocatorsDontCompareEqual) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> a(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> aa(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 101);
    EXPECT_EQ(alloc_counter, 2);
    EXPECT_EQ(dealloc_counter, 0);
    aa = std::move(a);  // This will copy as allocators don't compare equal.
  }
  EXPECT_EQ(alloc_counter, 3);
  EXPECT_EQ(dealloc_counter, 3);
}

TEST(ProtocolTest, CopiesAreDistinct) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  auto pp = p;
  EXPECT_EQ(p.count(), 42);
  EXPECT_EQ(pp.count(), 42);
}

TEST(ProtocolTest, CopiesOfDerivedObjectsAreDistinct) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  auto pp = p;
  pp.count();
  EXPECT_NE(p.count(), pp.count());
}

TEST(ProtocolTest, MoveRendersSourceValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  auto pp = std::move(p);
  EXPECT_TRUE(p.valueless_after_move());
}

TEST(ProtocolTest, ConstPropagation) {
  const xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  EXPECT_EQ(p.name(), "ALike");
}

TEST(ProtocolTest, CopyAssignment) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::in_place_type<ALike>, 101);
  p = pp;
  EXPECT_EQ(p.count(), 101);
}

TEST(ProtocolTest, CopyAssignmentSelf) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  p = p;
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_EQ(p.count(), 42);
}

TEST(ProtocolTest, MoveAssignment) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::in_place_type<ALike>, 101);
  p = std::move(pp);
  EXPECT_TRUE(pp.valueless_after_move());
  EXPECT_EQ(p.count(), 101);
}

TEST(ProtocolTest, MoveAssignmentSelf) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  p = std::move(p);
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_EQ(p.count(), 42);
}

TEST(ProtocolTest, NonMemberSwap) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::in_place_type<ALike>, 101);
  using std::swap;
  swap(p, pp);
  EXPECT_EQ(p.count(), 101);
  EXPECT_EQ(pp.count(), 42);
}

TEST(ProtocolTest, MemberSwap) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::in_place_type<ALike>, 101);
  p.swap(pp);
  EXPECT_EQ(p.count(), 101);
  EXPECT_EQ(pp.count(), 42);
}

TEST(ProtocolTest, MemberSwapWithSelf) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  p.swap(p);
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_EQ(p.count(), 42);
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

TEST(ProtocolTest, NonMemberSwapWhenAllocatorsDontCompareEqual) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, POCSTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        POCSTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    xyz::protocol<xyz::A, POCSTrackingAllocator<std::byte>> pp(
        std::allocator_arg,
        POCSTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 101);
    using std::swap;
    swap(p, pp);
    EXPECT_EQ(p.count(), 101);
    EXPECT_EQ(pp.count(), 42);
  }
}

TEST(ProtocolTest, MemberSwapWhenAllocatorsDontCompareEqual) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, POCSTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        POCSTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    xyz::protocol<xyz::A, POCSTrackingAllocator<std::byte>> pp(
        std::allocator_arg,
        POCSTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 101);
    p.swap(pp);
    EXPECT_EQ(p.count(), 101);
    EXPECT_EQ(pp.count(), 42);
  }
}

TEST(ProtocolTest, CopyFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(p);
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolTest, MoveFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::move(p));
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolTest, AllocatorExtendedCopyFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::allocator_arg, std::allocator<std::byte>(), p);
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolTest, AllocatorExtendedMoveFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::allocator_arg, std::allocator<std::byte>(),
                            std::move(p));
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolTest, AssignFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::in_place_type<ALike>, 101);
  ppp = p;
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolTest, MoveAssignFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::in_place_type<ALike>, 101);
  ppp = std::move(p);
  EXPECT_TRUE(ppp.valueless_after_move());
}

TEST(ProtocolTest, SwapFromValueless) {
  xyz::protocol<xyz::A> p(std::in_place_type<ALike>, 42);
  xyz::protocol<xyz::A> pp(std::move(p));
  xyz::protocol<xyz::A> ppp(std::in_place_type<ALike>, 101);
  using std::swap;
  swap(p, ppp);
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_TRUE(ppp.valueless_after_move());
}

class CovariantBImpl {
  std::string last_input_;

 public:
  // Testing discard: interface returns void, implementation returns int
  int process(const std::string& input) {
    last_input_ = input;
    return 42;
  }

  // Testing implicit conversion: returns an implicitly convertible struct
  // instead of std::vector
  struct VectorConvertible {
    operator std::vector<int>() const { return {1, 2, 3}; }
  };

  VectorConvertible get_results() const { return {}; }

  // Testing implicit conversion: returns const char* instead of bool
  const char* is_ready() const { return "ready"; }

  std::string last_input() const { return last_input_; }
};

TEST(ProtocolTest, CovariantReturns) {
  xyz::protocol<xyz::B> p(std::in_place_type<CovariantBImpl>);

  p.process("hello covariant");
  EXPECT_TRUE(p.is_ready());

  std::vector<int> results = p.get_results();
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(results[0], 1);
  EXPECT_EQ(results[1], 2);
  EXPECT_EQ(results[2], 3);
}

TEST(ProtocolViewTest, ViewFromMutableConcrete) {
  ALike a(10, "view_test");
  xyz::protocol_view<xyz::A> view(a);
  EXPECT_EQ(view.name(), "view_test");
  EXPECT_EQ(view.count(), 10);
  EXPECT_EQ(a.count(), 11);  // Ensure view mutated `a` directly.
}

TEST(ProtocolViewTest, ViewFromMutableProtocol) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 20, "proto_view");
  xyz::protocol_view<xyz::A> view(a);
  EXPECT_EQ(view.name(), "proto_view");
  EXPECT_EQ(view.count(), 20);
  EXPECT_EQ(a.count(), 21);  // Ensure view mutated `a` directly.
}

TEST(ProtocolViewTest, ConstViewFromMutableConcrete) {
  ALike a(10, "view_test");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "view_test");
}

TEST(ProtocolViewTest, ConstViewFromConstConcrete) {
  const ALike a(10, "view_test");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "view_test");
}

TEST(ProtocolViewTest, ConstViewFromMutableProtocol) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 20, "proto_view");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "proto_view");
}

TEST(ProtocolViewTest, ConstViewFromMutViewConcrete) {
  ALike a(10, "view_test");
  xyz::protocol_view<xyz::A> mut_view(a);
  xyz::protocol_view<const xyz::A> const_view(mut_view);
  EXPECT_EQ(const_view.name(), "view_test");
}

TEST(ProtocolViewTest, ConstViewFromMutViewProtocol) {
  xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 20, "proto_view");
  xyz::protocol_view<xyz::A> mut_view(a);
  xyz::protocol_view<const xyz::A> const_view(mut_view);
  EXPECT_EQ(const_view.name(), "proto_view");
}

TEST(ProtocolViewTest, ConstViewFromConstProtocol) {
  const xyz::protocol<xyz::A> a(std::in_place_type<ALike>, 20, "proto_view");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "proto_view");
}

TEST(ProtocolViewTest, ConstViewFromMutableConstALike) {
  ConstALike a("const_alike");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "const_alike");
}

TEST(ProtocolViewTest, ConstViewFromConstConstALike) {
  const ConstALike a("const_alike");
  xyz::protocol_view<const xyz::A> view(a);
  EXPECT_EQ(view.name(), "const_alike");
}

TEST(ProtocolViewTest, ViewConstnessRouting) {
  BLike b;
  xyz::protocol_view<xyz::B> view(b);
  EXPECT_FALSE(view.is_ready());
  view.process("view processing");
  EXPECT_TRUE(view.is_ready());
  auto results = view.get_results();
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0], 15);
}

TEST(ProtocolViewTest, ViewCopiesAreShallow) {
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

TEST(ProtocolViewTest, ViewMoveIsStandard) {
  ALike a(10, "move_test");
  xyz::protocol_view<xyz::A> view(a);
  xyz::protocol_view<xyz::A> view2 = std::move(view);

  // Moved-from view is still valid (it's just a pointer copy)
  EXPECT_EQ(view.name(), "move_test");
  EXPECT_EQ(view2.name(), "move_test");
}

TEST(ProtocolViewTest, PreventConstructionFromRValues) {
  // A mutable view should not be constructible from an r-value concrete type
  static_assert(!std::constructible_from<xyz::protocol_view<xyz::A>, ALike>,
                "protocol_view should not be constructible from r-value ALike");

  // A mutable view should not be constructible from an r-value protocol
  static_assert(
      !std::constructible_from<xyz::protocol_view<xyz::A>,
                               xyz::protocol<xyz::A>>,
      "protocol_view should not be constructible from r-value protocol");

  // A const view should not be constructible from an r-value concrete type
  static_assert(
      !std::constructible_from<xyz::protocol_view<const xyz::A>, ALike>,
      "protocol_view<const I> should not be constructible from r-value ALike");

  // A const view should not be constructible from an r-value protocol
  static_assert(!std::constructible_from<xyz::protocol_view<const xyz::A>,
                                         xyz::protocol<xyz::A>>,
                "protocol_view<const I> should not be constructible from "
                "r-value protocol");

  // A const view should not be constructible from an r-value const concrete
  // type
  static_assert(
      !std::constructible_from<xyz::protocol_view<const xyz::A>, const ALike>,
      "protocol_view<const I> should not be constructible from r-value const "
      "ALike");

  // A const view should not be constructible from an r-value const protocol
  static_assert(!std::constructible_from<xyz::protocol_view<const xyz::A>,
                                         const xyz::protocol<xyz::A>>,
                "protocol_view<const I> should not be constructible from "
                "r-value const protocol");

  // A mutable subset view should not be constructible from an r-value superset
  // protocol
  static_assert(!std::constructible_from<xyz::protocol_view<xyz::A_Subset>,
                                         xyz::protocol<xyz::A>>,
                "protocol_view should not be constructible from r-value "
                "compatible protocol");

  // A const subset view should not be constructible from an r-value superset
  // protocol
  static_assert(
      !std::constructible_from<xyz::protocol_view<const xyz::A_Subset>,
                               xyz::protocol<xyz::A>>,
      "protocol_view<const I> should not be constructible from r-value "
      "compatible protocol");

  // A const subset view should not be constructible from an r-value const
  // superset protocol
  static_assert(
      !std::constructible_from<xyz::protocol_view<const xyz::A_Subset>,
                               const xyz::protocol<xyz::A>>,
      "protocol_view<const I> should not be constructible from r-value const "
      "compatible protocol");
}

class DLike {
  int value_ = 0;

 public:
  int operator+(int x) const { return x + 10; }

  int operator-(int x) const { return x - 10; }

  int operator*(int x) const { return x * 10; }

  int operator/(int x) const { return x / 10; }

  int operator%(int x) const { return x % 10; }

  int operator^(int x) const { return x ^ 10; }

  int operator&(int x) const { return x & 10; }

  int operator|(int x) const { return x | 10; }

  int operator~() const { return ~10; }

  bool operator!() const { return !value_; }

  void operator=(int x) { value_ = x; }

  bool operator<(int x) const { return value_ < x; }

  bool operator>(int x) const { return value_ > x; }

  void operator+=(int x) { value_ += x; }

  void operator-=(int x) { value_ -= x; }

  void operator*=(int x) { value_ *= x; }

  void operator/=(int x) { value_ /= x; }

  void operator%=(int x) { value_ %= x; }

  void operator^=(int x) { value_ ^= x; }

  void operator&=(int x) { value_ &= x; }

  void operator|=(int x) { value_ |= x; }

  int operator<<(int x) const { return value_ << x; }

  int operator>>(int x) const { return value_ >> x; }

  void operator<<=(int x) { value_ <<= x; }

  void operator>>=(int x) { value_ >>= x; }

  bool operator==(int x) const { return value_ == x; }

  bool operator!=(int x) const { return value_ != x; }

  bool operator<=(int x) const { return value_ <= x; }

  bool operator>=(int x) const { return value_ >= x; }

  std::strong_ordering operator<=>(int x) const { return value_ <=> x; }

  bool operator&&(bool x) const { return value_ && x; }

  bool operator||(bool x) const { return value_ || x; }

  void operator++() { ++value_; }

  void operator--() { --value_; }

  int operator,(int x) const { return x; }

  int operator->*(int x) const { return x; }

  int* operator->() { return &value_; }

  int operator()() { return 42; }

  int operator[](int x) const { return x * 2; }
};

TEST(ProtocolTest, ProtocolDOperators) {
  xyz::protocol<xyz::D> d(std::in_place_type<DLike>);
  EXPECT_EQ(d + 5, 15);
  EXPECT_EQ(d - 15, 5);
  EXPECT_EQ(d * 2, 20);
  EXPECT_EQ(d / 100, 10);
  EXPECT_EQ(d % 15, 5);
  EXPECT_EQ(d ^ 5, 15);
  EXPECT_EQ(d & 15, 10);
  EXPECT_EQ(d | 5, 15);
  EXPECT_EQ(~d, ~10);
  EXPECT_TRUE(!d);

  d = 5;
  EXPECT_FALSE(!d);
  EXPECT_TRUE(d < 10);
  EXPECT_FALSE(d > 10);

  d += 5;
  EXPECT_TRUE(d == 10);
  d -= 5;
  EXPECT_TRUE(d == 5);
  d *= 2;
  EXPECT_TRUE(d == 10);
  d /= 2;
  EXPECT_TRUE(d == 5);
  d %= 3;
  EXPECT_TRUE(d == 2);
  d ^= 7;
  EXPECT_TRUE(d == 5);
  d &= 3;
  EXPECT_TRUE(d == 1);
  d |= 4;
  EXPECT_TRUE(d == 5);

  EXPECT_EQ(d << 1, 10);
  EXPECT_EQ(d >> 1, 2);

  d <<= 1;
  EXPECT_TRUE(d == 10);
  d >>= 1;
  EXPECT_TRUE(d == 5);

  EXPECT_TRUE(d != 10);
  EXPECT_TRUE(d <= 5);
  EXPECT_TRUE(d >= 5);
  EXPECT_TRUE((d <=> 5) == std::strong_ordering::equal);

  EXPECT_TRUE(d && true);
  EXPECT_TRUE(d || false);

  ++d;
  EXPECT_TRUE(d == 6);
  --d;
  EXPECT_TRUE(d == 5);

  EXPECT_EQ((d, 10), 10);
  EXPECT_EQ(d->*5, 5);

  *d.operator->() = 10;
  EXPECT_TRUE(d == 10);

  EXPECT_EQ(d(), 42);
  EXPECT_EQ(d[5], 10);
}

TEST(ProtocolViewTest, ProtocolViewDOperators) {
  DLike d_obj;
  xyz::protocol_view<xyz::D> d(d_obj);
  EXPECT_EQ(d + 5, 15);
  EXPECT_EQ(d - 15, 5);
  EXPECT_EQ(d * 2, 20);
  EXPECT_EQ(d / 100, 10);
  EXPECT_EQ(d % 15, 5);
  EXPECT_EQ(d ^ 5, 15);
  EXPECT_EQ(d & 15, 10);
  EXPECT_EQ(d | 5, 15);
  EXPECT_EQ(~d, ~10);
  EXPECT_TRUE(!d);

  d = 5;
  EXPECT_FALSE(!d);
  EXPECT_TRUE(d < 10);
  EXPECT_FALSE(d > 10);

  d += 5;
  EXPECT_TRUE(d == 10);
  d -= 5;
  EXPECT_TRUE(d == 5);
  d *= 2;
  EXPECT_TRUE(d == 10);
  d /= 2;
  EXPECT_TRUE(d == 5);
  d %= 3;
  EXPECT_TRUE(d == 2);
  d ^= 7;
  EXPECT_TRUE(d == 5);
  d &= 3;
  EXPECT_TRUE(d == 1);
  d |= 4;
  EXPECT_TRUE(d == 5);

  EXPECT_EQ(d << 1, 10);
  EXPECT_EQ(d >> 1, 2);

  d <<= 1;
  EXPECT_TRUE(d == 10);
  d >>= 1;
  EXPECT_TRUE(d == 5);

  EXPECT_TRUE(d != 10);
  EXPECT_TRUE(d <= 5);
  EXPECT_TRUE(d >= 5);
  EXPECT_TRUE((d <=> 5) == std::strong_ordering::equal);

  EXPECT_TRUE(d && true);
  EXPECT_TRUE(d || false);

  ++d;
  EXPECT_TRUE(d == 6);
  --d;
  EXPECT_TRUE(d == 5);

  EXPECT_EQ((d, 10), 10);
  EXPECT_EQ(d->*5, 5);

  *d.operator->() = 10;
  EXPECT_TRUE(d == 10);

  EXPECT_EQ(d(), 42);
  EXPECT_EQ(d[5], 10);
}

TEST(ProtocolViewTest, NarrowingConversion) {
  ALike a_obj;
  xyz::protocol_view<xyz::A> view_a(a_obj);

  // 1. Convert mutable view of A to const view of A_Subset
  xyz::protocol_view<const xyz::A_Subset> const_view_subset = view_a;
  EXPECT_EQ(const_view_subset.name(), "ALike");

  // 2. Convert mutable view of A to mutable view of A_Subset
  xyz::protocol_view<xyz::A_Subset> view_subset = view_a;
  EXPECT_EQ(view_subset.name(), "ALike");

  // 3. Convert const view of A to const view of A_Subset
  xyz::protocol_view<const xyz::A> const_view_a(a_obj);
  xyz::protocol_view<const xyz::A_Subset> const_view_subset2 = const_view_a;
  EXPECT_EQ(const_view_subset2.name(), "ALike");

  // 4. Convert mutable protocol of A to mutable view of A_Subset
  xyz::protocol<xyz::A> p_a(std::in_place_type<ALike>, 42, "proto_val");
  xyz::protocol_view<xyz::A_Subset> view_subset_from_p(p_a);
  EXPECT_EQ(view_subset_from_p.name(), "proto_val");

  // 5. Convert mutable protocol of A to const view of A_Subset
  xyz::protocol_view<const xyz::A_Subset> const_view_subset_from_p(p_a);
  EXPECT_EQ(const_view_subset_from_p.name(), "proto_val");

  // 6. Convert const protocol of A to const view of A_Subset
  const xyz::protocol<xyz::A> const_p_a(std::in_place_type<ALike>, 42,
                                        "const_proto_val");
  xyz::protocol_view<const xyz::A_Subset> const_view_subset_from_const_p(
      const_p_a);
  EXPECT_EQ(const_view_subset_from_const_p.name(), "const_proto_val");
}

TEST(ProtocolTest, NarrowingMoveConversionEqualAllocators) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(std::in_place_type<ALike>,
                                                     42);
  EXPECT_EQ(p.count(), 42);

  // Convert owning protocol using move constructor
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
      std::move(p);
  EXPECT_TRUE(p.valueless_after_move());
  EXPECT_FALSE(p_subset.valueless_after_move());
  EXPECT_EQ(p_subset.name(), "ALike");
}

TEST(ProtocolTest, NarrowingCopyConversion) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(std::in_place_type<ALike>,
                                                     42);
  EXPECT_EQ(p.count(), 42);

  // Convert owning protocol using copy constructor (clones the object)
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset(p);
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_FALSE(p_subset.valueless_after_move());
  EXPECT_EQ(p.name(), "ALike");
  EXPECT_EQ(p_subset.name(), "ALike");
}

TEST(ProtocolTest, CountAllocationsForNarrowingCopyConversion) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> p(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);

    // Convert via copy constructor - this should clone the underlying object
    xyz::protocol<xyz::A_Subset, xyz::TrackingAllocator<std::byte>> p_subset(p);
    EXPECT_FALSE(p.valueless_after_move());
    EXPECT_FALSE(p_subset.valueless_after_move());
    EXPECT_EQ(alloc_counter, 2);
    EXPECT_EQ(dealloc_counter, 0);
  }
  EXPECT_EQ(alloc_counter, 2);
  EXPECT_EQ(dealloc_counter, 2);
}

TEST(ProtocolTest, NarrowingCopyConversionFromValueless) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(std::in_place_type<ALike>,
                                                     42);
  xyz::protocol<xyz::A, std::allocator<std::byte>> p2 = std::move(p);
  EXPECT_TRUE(p.valueless_after_move());

  // Copying a valueless protocol should yield a valueless protocol
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset(p);
  EXPECT_TRUE(p_subset.valueless_after_move());
}

TEST(ProtocolTest, NarrowingCopyConversionNonEqualAllocators) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);

    // Copy-convert with different allocator instance (non-equal allocators)
    NonEqualTrackingAllocator<std::byte> target_alloc(&alloc_counter,
                                                      &dealloc_counter);
    xyz::protocol<xyz::A_Subset, NonEqualTrackingAllocator<std::byte>> p_subset(
        std::allocator_arg, target_alloc, p);

    EXPECT_FALSE(p.valueless_after_move());
    EXPECT_FALSE(p_subset.valueless_after_move());
    EXPECT_EQ(p.name(), "ALike");
    EXPECT_EQ(p_subset.name(), "ALike");

    // 1 allocation for source object, plus 1 allocation for cloned object on
    // the target allocator
    EXPECT_EQ(alloc_counter, 2);
    EXPECT_EQ(dealloc_counter, 0);
  }
  // All allocations should be cleaned up
  EXPECT_EQ(alloc_counter, 2);
  EXPECT_EQ(dealloc_counter, 2);
}

TEST(ProtocolTest, NarrowingMoveConversionNonEqualAllocators) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);

    // Convert with different allocator instances (non-equal allocators)
    // This will force allocating a copy of the underlying object using the
    // destination's allocator
    xyz::protocol<xyz::A_Subset, NonEqualTrackingAllocator<std::byte>> p_subset(
        std::move(p));

    EXPECT_FALSE(p_subset.valueless_after_move());
    EXPECT_EQ(p_subset.name(), "ALike");
    // Since allocators compare non-equal, it must have allocated on the new
    // allocator, and old remains valid/valueless? Wait, the standard move
    // constructor for different allocators will move-construct the content,
    // which allocates 1 and destroys/deallocates the source object.
    EXPECT_EQ(alloc_counter, 2);
  }
  // All allocations should be cleaned up
  EXPECT_EQ(alloc_counter, 2);
  EXPECT_EQ(dealloc_counter, 2);
}

TEST(ProtocolTest, CountAllocationsForNarrowingMoveConversion) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);

    // Perform narrowing move conversion with equal allocators (stolen pointer)
    xyz::protocol<xyz::A_Subset, xyz::TrackingAllocator<std::byte>> aa(
        std::move(a));

    EXPECT_TRUE(a.valueless_after_move());
    EXPECT_FALSE(aa.valueless_after_move());
    // Zero new allocations and zero deallocations during conversion
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);
  }
  // The underlying object is destroyed at block exit
  EXPECT_EQ(alloc_counter, 1);
  EXPECT_EQ(dealloc_counter, 1);
}

TEST(ProtocolTest, AllocatorExtendedNarrowingMoveConversionEqualAllocators) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, xyz::TrackingAllocator<std::byte>> a(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);

    // Perform allocator-extended narrowing move conversion with equal
    // allocators (stolen pointer)
    xyz::protocol<xyz::A_Subset, xyz::TrackingAllocator<std::byte>> aa(
        std::allocator_arg,
        xyz::TrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::move(a));

    EXPECT_TRUE(a.valueless_after_move());
    EXPECT_FALSE(aa.valueless_after_move());
    // Zero new allocations and zero deallocations during conversion
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);
  }
  EXPECT_EQ(alloc_counter, 1);
  EXPECT_EQ(dealloc_counter, 1);
}

TEST(ProtocolTest, AllocatorExtendedNarrowingMoveConversionNonEqualAllocators) {
  unsigned alloc_counter = 0;
  unsigned dealloc_counter = 0;
  {
    xyz::protocol<xyz::A, NonEqualTrackingAllocator<std::byte>> p(
        std::allocator_arg,
        NonEqualTrackingAllocator<std::byte>(&alloc_counter, &dealloc_counter),
        std::in_place_type<ALike>, 42);
    EXPECT_EQ(alloc_counter, 1);
    EXPECT_EQ(dealloc_counter, 0);

    // Perform allocator-extended narrowing move conversion with different
    // allocator instance (non-equal allocators) This will force allocating a
    // copy of the underlying object using the destination's allocator
    NonEqualTrackingAllocator<std::byte> target_alloc(&alloc_counter,
                                                      &dealloc_counter);
    xyz::protocol<xyz::A_Subset, NonEqualTrackingAllocator<std::byte>> p_subset(
        std::allocator_arg, target_alloc, std::move(p));

    EXPECT_TRUE(p.valueless_after_move());
    EXPECT_FALSE(p_subset.valueless_after_move());
    EXPECT_EQ(p_subset.name(), "ALike");

    // 1 allocation for source object, plus 1 allocation for moved/cloned object
    // on the target allocator, and the source object is deallocated immediately
    // because we moved from it.
    EXPECT_EQ(alloc_counter, 2);
    EXPECT_EQ(dealloc_counter, 1);
  }
  // All allocations should be cleaned up
  EXPECT_EQ(alloc_counter, 2);
  EXPECT_EQ(dealloc_counter, 2);
}

TEST(ProtocolTest, NarrowingMoveConversionFromValueless) {
  xyz::protocol<xyz::A, std::allocator<std::byte>> p(std::in_place_type<ALike>,
                                                     42);
  xyz::protocol<xyz::A, std::allocator<std::byte>> p2 = std::move(p);
  EXPECT_TRUE(p.valueless_after_move());

  // Converting a valueless protocol should yield a valueless protocol
  xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
      std::move(p);
  EXPECT_TRUE(p_subset.valueless_after_move());
}

TEST(ProtocolViewTest, NarrowingConversionCombinations) {
  ALike a_obj;
  xyz::protocol_view<xyz::A> view_a(a_obj);

  // 1. Convert mutable view of A to const view of A_Subset
  xyz::protocol_view<const xyz::A_Subset> const_view_subset = view_a;
  EXPECT_EQ(const_view_subset.name(), "ALike");

  // 2. Convert mutable view of A to mutable view of A_Subset
  xyz::protocol_view<xyz::A_Subset> view_subset = view_a;
  EXPECT_EQ(view_subset.name(), "ALike");

  // 3. Convert const view of A to const view of A_Subset
  xyz::protocol_view<const xyz::A> const_view_a(a_obj);
  xyz::protocol_view<const xyz::A_Subset> const_view_subset2 = const_view_a;
  EXPECT_EQ(const_view_subset2.name(), "ALike");
}

TEST(ProtocolTest, NarrowingConversionConcurrentAccess) {
  constexpr int kNumThreads = 10;
  std::vector<std::thread> threads;
  std::atomic<bool> start_signal{false};

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&start_signal]() {
      while (!start_signal.load()) {
        std::this_thread::yield();
      }
      ALike a_obj;
      xyz::protocol_view<xyz::A> view_a(a_obj);

      // Perform view conversions concurrently
      xyz::protocol_view<const xyz::A_Subset> const_view = view_a;
      EXPECT_EQ(const_view.name(), "ALike");

      xyz::protocol_view<xyz::A_Subset> mut_view = view_a;
      EXPECT_EQ(mut_view.name(), "ALike");

      // Perform owning conversions concurrently
      xyz::protocol<xyz::A, std::allocator<std::byte>> p(
          std::in_place_type<ALike>, 42);
      xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
          std::move(p);
      EXPECT_EQ(p_subset.name(), "ALike");
    });
  }

  // Release all threads to run concurrently
  start_signal.store(true);

  for (auto& t : threads) {
    t.join();
  }
}

TEST(ProtocolTest, NarrowingConversionConcurrentStressing) {
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

        // Concurrently query view conversions (often hitting cache)
        xyz::protocol_view<const xyz::A_Subset> const_view = view_a;
        EXPECT_EQ(const_view.name(), "ALike");

        xyz::protocol_view<xyz::A_Subset> mut_view = view_a;
        EXPECT_EQ(mut_view.name(), "ALike");

        // Concurrently query owning conversions (often hitting cache)
        xyz::protocol<xyz::A, std::allocator<std::byte>> p(
            std::in_place_type<ALike>, 42);
        xyz::protocol<xyz::A_Subset, std::allocator<std::byte>> p_subset =
            std::move(p);
        EXPECT_EQ(p_subset.name(), "ALike");

        // Use custom allocator to trigger a different map entry
        xyz::protocol<xyz::A, std::allocator<char>> p_char(
            std::in_place_type<ALike>, 42);
        xyz::protocol<xyz::A_Subset, std::allocator<char>> p_subset_char =
            std::move(p_char);
        EXPECT_EQ(p_subset_char.name(), "ALike");
      }
    });
  }

  // Release all threads to run concurrently and contend on cache
  // lookup/creation
  start_signal.store(true);

  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace

#ifdef XYZ_PROTOCOL_ENABLE_REFLECTION

template <class R, class... Args>
struct Function {
  R operator()(Args... args) const;
};

template <class R, class... Args>
struct RvalueParameterFunction {
  R operator()(Args&&... args) const;
};

namespace {

struct SquareAdder {
  int operator()(int x, int y) const { return x * x + y * y; }
};

struct StringConcatenator {
  std::string operator()(std::string_view a, std::string_view b) const {
    return std::string(a) + std::string(b);
  }
};

struct RvalueIntAdder {
  int operator()(int&& x) const { return x + 10; }
};

struct NarrowingComputeInterface {
  double compute(double x) const;
};

struct FloatOnlyImplementation {
  float compute(float x) const { return x * 2.0f; }
};

struct FloatComputeInterface {
  double compute(float x) const;
};

struct OverloadedComputeImplementation {
  int compute(int x) const { return x + 1; }

  double compute(double x) const { return x * 2; }
};

TEST(ProtocolReflectionTest, ImplicitConversionConformance) {
  // FloatOnlyImplementation::compute(float) does not exactly match
  // NarrowingComputeInterface::compute(double); this now conforms (and
  // dispatches through the implicit double->float/float->double
  // conversions) the same way the Python backend's requires-expression
  // already does, matching real C++ overload resolution.
  static_assert(
      xyz::reflection_protocol_const_concept<FloatOnlyImplementation,
                                             NarrowingComputeInterface>);
  xyz::protocol<NarrowingComputeInterface> p(
      std::in_place_type<FloatOnlyImplementation>);
  EXPECT_DOUBLE_EQ(p.compute(21.0), 42.0);
}

TEST(ProtocolReflectionTest, ImplicitConversionPicksBestOverload) {
  // OverloadedComputeImplementation offers compute(int) and compute(double);
  // FloatComputeInterface's compute(float) must resolve to compute(double),
  // since float->double is a better conversion than float->int -- proving
  // real overload resolution over the merged candidate set, not just "any
  // invocable candidate".
  xyz::protocol<FloatComputeInterface> p(
      std::in_place_type<OverloadedComputeImplementation>);
  EXPECT_DOUBLE_EQ(p.compute(3.0f), 6.0);
}

struct TouchInterface {
  int touch();
};

struct DualConstnessImplementation {
  int touch() { return 10; }

  int touch() const { return 20; }
};

TEST(ProtocolReflectionTest, PrefersNonConstOverloadOnNonConstAccess) {
  // DualConstnessImplementation declares both touch() and touch() const;
  // TouchInterface::touch() is non-const, so real overload resolution over
  // the merged candidate set must prefer the non-const overload, exactly
  // as calling touch() directly on a non-const object would -- not treat
  // the two as ambiguous. This is the case merging candidates with a single
  // shared constness (rather than each candidate's own) would break.
  xyz::protocol<TouchInterface> p(
      std::in_place_type<DualConstnessImplementation>);
  EXPECT_EQ(p.touch(), 10);
}

TEST(ProtocolReflectionTest, ClassTemplateInstantiationAsInterface) {
  // Test Function<int, int, int> without any opt-in variable template
  xyz::protocol<Function<int, int, int>> integer_function(SquareAdder{});
  EXPECT_EQ(integer_function(3, 4), 25);

  xyz::protocol_view<const Function<int, int, int>> integer_function_view(
      integer_function);
  EXPECT_EQ(integer_function_view(5, 12), 169);

  // Test Function<std::string, std::string_view, std::string_view>
  xyz::protocol<Function<std::string, std::string_view, std::string_view>>
      string_function(StringConcatenator{});
  EXPECT_EQ(string_function("hello ", "world"), "hello world");

  // Test RvalueParameterFunction<int, int> with rvalue reference parameter
  // (int&&)
  xyz::protocol<RvalueParameterFunction<int, int>> reference_function(
      RvalueIntAdder{});
  EXPECT_EQ(reference_function(5), 15);
}

}  // namespace

#endif

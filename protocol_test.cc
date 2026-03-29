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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "generated/protocol_A.h"
#include "generated/protocol_B.h"
#include "generated/protocol_C.h"
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

}  // namespace

// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view: type traits, special member functions, constructability,
// protocol_cast and target_type.

#include <gtest/gtest.h>

#include <concepts>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "xyz/protocol.hh"

using xyz::detail::is_protocol_v;
using xyz::detail::is_protocol_view_v;
using xyz::detail::is_valid_interface;

// Tests for protocol's small buffer optimization. Whether an object is in the
// small buffer is observed through allocation counts: an object in the buffer
// is not allocated.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "xyz/protocol.hh"
#include "xyz/tests/tracking_allocator.h"

// NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the tests
// check moved-from protocols.

namespace xyz::tests {
namespace {

using xyz::detail::fits_in_small_buffer_v;
using xyz::detail::small_buffer_alignment;
using xyz::detail::small_buffer_size;

using Alloc = TrackingAllocator<std::byte>;

struct Interface {
  int value() const;
};

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

// Conforms to `Interface`. `PaddingSize` bytes of padding decide whether it
// fits in the small buffer.
template <std::size_t PaddingSize>
class Tracker {
  Counts* counts_;
  int value_;
  [[maybe_unused]] std::array<std::byte, PaddingSize> padding_{};

 public:
  Tracker(Counts* counts, int value) noexcept : counts_(counts), value_(value) {
    ++counts_->live;
  }

  Tracker(const Tracker& other) : counts_(other.counts_), value_(other.value_) {
    ++counts_->live;
    ++counts_->copies;
  }

  Tracker(Tracker&& other) noexcept
      : counts_(other.counts_), value_(other.value_) {
    ++counts_->live;
    ++counts_->moves;
  }

  Tracker& operator=(const Tracker&) = delete ("Unused");
  Tracker& operator=(Tracker&&) = delete ("Unused");

  ~Tracker() { --counts_->live; }

  int value() const { return value_; }
};

using SmallTracker = Tracker<0>;
using LargeTracker = Tracker<small_buffer_size>;

static_assert(fits_in_small_buffer_v<SmallTracker, Alloc>);
static_assert(!fits_in_small_buffer_v<LargeTracker, Alloc>);

// Counts the allocations made through the allocators it hands out.
struct Allocations {
  unsigned allocations = 0;
  unsigned deallocations = 0;

  Alloc allocator() { return Alloc(&allocations, &deallocations); }
};

using TrackingProtocol = protocol<Interface, Alloc>;

// ---------------------------------------------------------------------------
// Layout and eligibility.
// ---------------------------------------------------------------------------

TEST(SmallBufferTest, ProtocolIsFourPointersWithTheDefaultAllocator) {
  static_assert(sizeof(protocol<Interface>) == 4 * sizeof(void*));
}

TEST(SmallBufferTest, TypeOfExactlyTheBufferSizeFits) {
  struct Exact {
    std::array<std::byte, small_buffer_size> bytes;
  };

  static_assert(fits_in_small_buffer_v<Exact, Alloc>);
}

TEST(SmallBufferTest, TypeLargerThanTheBufferDoesNotFit) {
  struct Larger {
    std::array<std::byte, small_buffer_size + 1> bytes;
  };

  static_assert(!fits_in_small_buffer_v<Larger, Alloc>);
}

TEST(SmallBufferTest, OveralignedTypeDoesNotFit) {
  struct alignas(2 * small_buffer_alignment) Overaligned {
    std::byte byte;
  };

  static_assert(sizeof(Overaligned) <= small_buffer_size);
  static_assert(!fits_in_small_buffer_v<Overaligned, Alloc>);
}

TEST(SmallBufferTest, TypeWithThrowingMoveDoesNotFit) {
  struct ThrowingMove {
    ThrowingMove() = default;

    ThrowingMove(ThrowingMove&&) {}
  };

  static_assert(!fits_in_small_buffer_v<ThrowingMove, Alloc>);
}

// Objects in the buffer are constructed without the allocator, so an
// allocator-aware type is always allocated.
struct AllocatorAware {
  using allocator_type = Alloc;

  int value() const { return 7; }
};

TEST(SmallBufferTest, AllocatorAwareTypeIsAllocated) {
  static_assert(sizeof(AllocatorAware) <= small_buffer_size);
  static_assert(!fits_in_small_buffer_v<AllocatorAware, Alloc>);

  Allocations allocations;
  {
    TrackingProtocol p(std::allocator_arg, allocations.allocator(),
                       AllocatorAware{});
    EXPECT_EQ(p.value(), 7);
    EXPECT_EQ(allocations.allocations, 1);
  }
  EXPECT_EQ(allocations.deallocations, 1);
}

// ---------------------------------------------------------------------------
// Construction.
// ---------------------------------------------------------------------------

TEST(SmallBufferTest, SmallObjectIsNotAllocated) {
  Counts counts;
  Allocations allocations;

  TrackingProtocol p(std::allocator_arg, allocations.allocator(),
                     SmallTracker(&counts, 1));

  EXPECT_EQ(p.value(), 1);
  EXPECT_EQ(allocations.allocations, 0);
  EXPECT_EQ(counts.live, 1);
}

TEST(SmallBufferTest, LargeObjectIsAllocated) {
  Counts counts;
  Allocations allocations;
  {
    TrackingProtocol p(std::allocator_arg, allocations.allocator(),
                       LargeTracker(&counts, 1));

    EXPECT_EQ(p.value(), 1);
    EXPECT_EQ(allocations.allocations, 1);
  }
  EXPECT_EQ(allocations.deallocations, 1);
}

TEST(SmallBufferTest, InPlaceConstructionOfSmallObject) {
  Counts counts;
  Allocations allocations;

  TrackingProtocol p(std::allocator_arg, allocations.allocator(),
                     std::in_place_type<SmallTracker>, &counts, 3);

  EXPECT_EQ(p.value(), 3);
  EXPECT_EQ(allocations.allocations, 0);
  EXPECT_EQ(counts.moves, 0);
}

// ---------------------------------------------------------------------------
// Copy and move construction.
// ---------------------------------------------------------------------------

template <typename T>
void check_copy_construction(unsigned expected_allocations) {
  Counts counts;
  Allocations allocations;
  {
    TrackingProtocol p(std::allocator_arg, allocations.allocator(),
                       std::in_place_type<T>, &counts, 1);
    TrackingProtocol copy(p);

    EXPECT_EQ(copy.value(), 1);
    EXPECT_EQ(p.value(), 1);
    EXPECT_EQ(counts.copies, 1);
    EXPECT_EQ(counts.live, 2);
    EXPECT_EQ(allocations.allocations, expected_allocations);
  }
  EXPECT_EQ(allocations.deallocations, expected_allocations);
}

TEST(SmallBufferTest, CopyConstructSmall) {
  check_copy_construction<SmallTracker>(0);
}

TEST(SmallBufferTest, CopyConstructLarge) {
  check_copy_construction<LargeTracker>(2);
}

// A small object is moved out of the source's buffer; a large one changes
// owner without being moved.
template <typename T>
void check_move_construction(std::size_t expected_moves) {
  Counts counts;
  Allocations allocations;

  TrackingProtocol p(std::allocator_arg, allocations.allocator(),
                     std::in_place_type<T>, &counts, 1);
  TrackingProtocol moved(std::move(p));

  EXPECT_EQ(moved.value(), 1);
  EXPECT_TRUE(valueless_after_move(p));
  EXPECT_EQ(counts.moves, expected_moves);
  EXPECT_EQ(counts.live, 1);
}

TEST(SmallBufferTest, MoveConstructSmall) {
  check_move_construction<SmallTracker>(1);
}

TEST(SmallBufferTest, MoveConstructLarge) {
  check_move_construction<LargeTracker>(0);
}

TEST(SmallBufferTest, MoveConstructSmallWithUnequalAllocator) {
  Counts counts;
  Allocations source_allocations;
  Allocations target_allocations;

  TrackingProtocol p(std::allocator_arg, source_allocations.allocator(),
                     SmallTracker(&counts, 1));
  counts.moves = 0;
  TrackingProtocol moved(std::allocator_arg, target_allocations.allocator(),
                         std::move(p));

  EXPECT_EQ(moved.value(), 1);
  EXPECT_TRUE(valueless_after_move(p));
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(counts.live, 1);
  EXPECT_EQ(source_allocations.allocations, 0);
  EXPECT_EQ(target_allocations.allocations, 0);
}

TEST(SmallBufferTest, MoveConstructLargeWithUnequalAllocator) {
  Counts counts;
  Allocations source_allocations;
  Allocations target_allocations;
  {
    TrackingProtocol p(std::allocator_arg, source_allocations.allocator(),
                       LargeTracker(&counts, 1));
    counts.moves = 0;
    TrackingProtocol moved(std::allocator_arg, target_allocations.allocator(),
                           std::move(p));

    EXPECT_EQ(moved.value(), 1);
    EXPECT_TRUE(valueless_after_move(p));
    EXPECT_EQ(counts.moves, 1);
    EXPECT_EQ(counts.live, 1);
    EXPECT_EQ(source_allocations.deallocations, 1);
    EXPECT_EQ(target_allocations.allocations, 1);
  }
  EXPECT_EQ(target_allocations.deallocations, 1);
}

// ---------------------------------------------------------------------------
// Copy and move assignment.
// ---------------------------------------------------------------------------

template <typename Target, typename Source>
void check_copy_assignment() {
  Counts counts;
  Allocations allocations;

  TrackingProtocol target(std::allocator_arg, allocations.allocator(),
                          std::in_place_type<Target>, &counts, 1);
  TrackingProtocol source(std::allocator_arg, allocations.allocator(),
                          std::in_place_type<Source>, &counts, 2);
  target = source;

  EXPECT_EQ(target.value(), 2);
  EXPECT_EQ(source.value(), 2);
  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(counts.live, 2);
}

TEST(SmallBufferTest, CopyAssignSmallToSmall) {
  check_copy_assignment<SmallTracker, SmallTracker>();
}

TEST(SmallBufferTest, CopyAssignLargeToSmall) {
  check_copy_assignment<SmallTracker, LargeTracker>();
}

TEST(SmallBufferTest, CopyAssignSmallToLarge) {
  check_copy_assignment<LargeTracker, SmallTracker>();
}

TEST(SmallBufferTest, CopyAssignLargeToLarge) {
  check_copy_assignment<LargeTracker, LargeTracker>();
}

template <typename Target, typename Source>
void check_move_assignment(std::size_t expected_moves) {
  Counts counts;
  Allocations allocations;

  TrackingProtocol target(std::allocator_arg, allocations.allocator(),
                          std::in_place_type<Target>, &counts, 1);
  TrackingProtocol source(std::allocator_arg, allocations.allocator(),
                          std::in_place_type<Source>, &counts, 2);
  target = std::move(source);

  EXPECT_EQ(target.value(), 2);
  EXPECT_TRUE(valueless_after_move(source));
  EXPECT_EQ(counts.moves, expected_moves);
  EXPECT_EQ(counts.live, 1);
}

TEST(SmallBufferTest, MoveAssignSmallToSmall) {
  check_move_assignment<SmallTracker, SmallTracker>(1);
}

TEST(SmallBufferTest, MoveAssignLargeToSmall) {
  check_move_assignment<SmallTracker, LargeTracker>(0);
}

TEST(SmallBufferTest, MoveAssignSmallToLarge) {
  check_move_assignment<LargeTracker, SmallTracker>(1);
}

TEST(SmallBufferTest, MoveAssignLargeToLarge) {
  check_move_assignment<LargeTracker, LargeTracker>(0);
}

// With unequal allocators that do not propagate, the source's object is moved
// into a temporary that uses the target's allocator, then into the target.
template <typename Source>
void check_move_assignment_with_unequal_allocator(
    std::size_t expected_moves, unsigned expected_target_allocations) {
  Counts counts;
  Allocations source_allocations;
  Allocations target_allocations;
  {
    TrackingProtocol target(std::allocator_arg, target_allocations.allocator(),
                            SmallTracker(&counts, 1));
    TrackingProtocol source(std::allocator_arg, source_allocations.allocator(),
                            std::in_place_type<Source>, &counts, 2);
    counts.moves = 0;
    target = std::move(source);

    EXPECT_EQ(target.value(), 2);
    EXPECT_TRUE(valueless_after_move(source));
    EXPECT_EQ(counts.moves, expected_moves);
    EXPECT_EQ(counts.live, 1);
    EXPECT_EQ(target_allocations.allocations, expected_target_allocations);
    EXPECT_EQ(source_allocations.allocations, source_allocations.deallocations);
  }
  EXPECT_EQ(target_allocations.deallocations, expected_target_allocations);
}

TEST(SmallBufferTest, MoveAssignSmallWithUnequalAllocator) {
  check_move_assignment_with_unequal_allocator<SmallTracker>(2, 0);
}

TEST(SmallBufferTest, MoveAssignLargeWithUnequalAllocator) {
  check_move_assignment_with_unequal_allocator<LargeTracker>(1, 1);
}

// ---------------------------------------------------------------------------
// Swap.
// ---------------------------------------------------------------------------

// Swapping moves each object in a small buffer once through a temporary;
// large objects change owner without being moved.
template <typename Left, typename Right>
void check_swap(std::size_t expected_moves) {
  Counts counts;
  Allocations allocations;

  TrackingProtocol left(std::allocator_arg, allocations.allocator(),
                        std::in_place_type<Left>, &counts, 1);
  TrackingProtocol right(std::allocator_arg, allocations.allocator(),
                         std::in_place_type<Right>, &counts, 2);
  swap(left, right);

  EXPECT_EQ(left.value(), 2);
  EXPECT_EQ(right.value(), 1);
  EXPECT_EQ(counts.moves, expected_moves);
  EXPECT_EQ(counts.live, 2);
}

TEST(SmallBufferTest, SwapSmallAndSmall) {
  check_swap<SmallTracker, SmallTracker>(3);
}

TEST(SmallBufferTest, SwapSmallAndLarge) {
  check_swap<SmallTracker, LargeTracker>(2);
}

TEST(SmallBufferTest, SwapLargeAndSmall) {
  check_swap<LargeTracker, SmallTracker>(1);
}

TEST(SmallBufferTest, SwapLargeAndLarge) {
  check_swap<LargeTracker, LargeTracker>(0);
}

TEST(SmallBufferTest, SelfSwapLeavesValueUnchanged) {
  Counts counts;

  protocol<Interface> p(SmallTracker(&counts, 1));
  p.swap(p);

  EXPECT_EQ(p.value(), 1);
  EXPECT_EQ(counts.live, 1);
}

TEST(SmallBufferTest, SwapWithValueless) {
  Counts counts;

  protocol<Interface> p(SmallTracker(&counts, 1));
  protocol<Interface> moved(std::move(p));
  protocol<Interface> q(SmallTracker(&counts, 2));
  swap(p, q);

  EXPECT_EQ(p.value(), 2);
  EXPECT_TRUE(valueless_after_move(q));
  EXPECT_EQ(counts.live, 2);
}

TEST(SmallBufferTest, SwapRequiresMoveConstructibleInterface) {
  struct ImmovableInterface {
    ImmovableInterface(ImmovableInterface&&) = delete;
    int value() const;
  };

  static_assert(std::is_nothrow_swappable_v<protocol<Interface>>);
  static_assert(!std::is_swappable_v<protocol<ImmovableInterface>>);
}

// ---------------------------------------------------------------------------
// Exception specifications. These are a strengthening by this
// implementation, not part of the proposed specification: whether an object
// is in the small buffer is up to the implementation.
// ---------------------------------------------------------------------------

template <typename T>
struct ThrowingDefaultAllocator : std::allocator<T> {
  ThrowingDefaultAllocator() {}

  template <typename U>
  ThrowingDefaultAllocator(const ThrowingDefaultAllocator<U>&) noexcept {}

  template <typename Other>
  struct rebind {
    using other = ThrowingDefaultAllocator<Other>;
  };
};

TEST(SmallBufferTest, ConstructingSmallObjectFromRvalueIsNoexcept) {
  static_assert(
      std::is_nothrow_constructible_v<protocol<Interface>, SmallTracker>);
  static_assert(
      std::is_nothrow_constructible_v<protocol<Interface>,
                                      std::in_place_type_t<SmallTracker>,
                                      Counts*, int>);
  static_assert(
      std::is_nothrow_constructible_v<TrackingProtocol, std::allocator_arg_t,
                                      const Alloc&, SmallTracker>);
}

TEST(SmallBufferTest, ConstructingWithThrowingConstructorIsNotNoexcept) {
  static_assert(!std::is_nothrow_constructible_v<protocol<Interface>,
                                                 const SmallTracker&>);
}

TEST(SmallBufferTest, ConstructingLargeObjectIsNotNoexcept) {
  static_assert(
      !std::is_nothrow_constructible_v<protocol<Interface>, LargeTracker>);
}

TEST(SmallBufferTest, DefaultConstructingThrowingAllocatorIsNotNoexcept) {
  using ThrowingDefaultProtocol =
      protocol<Interface, ThrowingDefaultAllocator<std::byte>>;

  static_assert(
      !std::is_nothrow_constructible_v<ThrowingDefaultProtocol, SmallTracker>);
  static_assert(std::is_nothrow_constructible_v<
                ThrowingDefaultProtocol, std::allocator_arg_t,
                const ThrowingDefaultAllocator<std::byte>&, SmallTracker>);
}

// ---------------------------------------------------------------------------
// Views and casts.
// ---------------------------------------------------------------------------

template <typename T>
void check_view_and_cast() {
  Counts counts;

  protocol<Interface> p(std::in_place_type<T>, &counts, 5);
  const protocol<Interface>& const_p = p;
  protocol_view<Interface> view(p);
  protocol_view<const Interface> const_view(const_p);

  EXPECT_EQ(view.value(), 5);
  EXPECT_EQ(const_view.value(), 5);
  EXPECT_EQ(target_type(view), typeid(T));
  EXPECT_EQ(protocol_cast<T&>(p).value(), 5);
  EXPECT_EQ(protocol_cast<T>(&p)->value(), 5);
  EXPECT_EQ(protocol_cast<T>(&const_p)->value(), 5);
  EXPECT_EQ(&protocol_cast<T>(view), protocol_cast<T>(&p));
  EXPECT_EQ(&protocol_cast<T>(const_view), protocol_cast<T>(&p));
}

TEST(SmallBufferTest, ViewAndCastOfSmallObject) {
  check_view_and_cast<SmallTracker>();
}

TEST(SmallBufferTest, ViewAndCastOfLargeObject) {
  check_view_and_cast<LargeTracker>();
}

// A large object does not move when its protocol is moved, so a view of it
// stays valid.
TEST(SmallBufferTest, ViewOfLargeObjectSurvivesMove) {
  Counts counts;

  protocol<Interface> p(LargeTracker(&counts, 5));
  protocol_view<Interface> view(p);
  protocol<Interface> moved(std::move(p));

  EXPECT_EQ(view.value(), 5);
  EXPECT_EQ(&protocol_cast<LargeTracker>(view),
            protocol_cast<LargeTracker>(&moved));
}

// ---------------------------------------------------------------------------
// Constant evaluation, where objects are always allocated.
// ---------------------------------------------------------------------------

struct Literal {
  int value_;

  constexpr int value() const { return value_; }
};

static_assert(fits_in_small_buffer_v<Literal, std::allocator<std::byte>>);

TEST(SmallBufferTest, SpecialMembersInConstantEvaluation) {
  static_assert([] consteval {
    protocol<Interface> p(Literal{1});
    protocol<Interface> copy(p);
    protocol<Interface> moved(std::move(copy));
    protocol<Interface> q(Literal{2});
    q = moved;
    q = std::move(moved);
    swap(p, q);
    return protocol_cast<Literal>(&p)->value() == 1 &&
           protocol_cast<Literal>(&q)->value() == 1 &&
           valueless_after_move(copy) && valueless_after_move(moved);
  }());
}

}  // namespace
}  // namespace xyz::tests

// NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)

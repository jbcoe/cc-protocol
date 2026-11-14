// Tests for assigning a conforming object to a protocol.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "xyz/protocol.hh"
#include "xyz/tests/test_helpers.h"
#include "xyz/tests/tracking_allocator.h"

namespace xyz::tests {
namespace {

using Alloc = TrackingAllocator<std::byte>;

struct Interface {
  int value() const;
};

struct MoveOnlyInterface {
  int value() const;

  MoveOnlyInterface() = default;
  MoveOnlyInterface(const MoveOnlyInterface&) = delete ("Unused");
  MoveOnlyInterface(MoveOnlyInterface&&) = default;
  MoveOnlyInterface& operator=(const MoveOnlyInterface&) = delete ("Unused");
  MoveOnlyInterface& operator=(MoveOnlyInterface&&) = default;
  ~MoveOnlyInterface() = default;
};

struct ImmovableInterface {
  int value() const;

  ImmovableInterface() = default;
  ImmovableInterface(const ImmovableInterface&) = delete ("Unused");
  ImmovableInterface(ImmovableInterface&&) = delete ("Unused");
  ImmovableInterface& operator=(const ImmovableInterface&) = delete ("Unused");
  ImmovableInterface& operator=(ImmovableInterface&&) = delete ("Unused");
  ~ImmovableInterface() = default;
};

// Fits in the small buffer.
struct Small {
  int value_;

  constexpr int value() const { return value_; }
};

// Too large for the small buffer, so it is allocated.
struct Large {
  int value_;
  TooLargeForSmallBuffer padding_{};

  constexpr int value() const { return value_; }
};

static_assert(detail::fits_in_small_buffer_v<Small, Alloc>);
static_assert(!detail::fits_in_small_buffer_v<Large, Alloc>);

// Counts the copies and moves of `Tracker`s.
struct Counts {
  std::size_t copies = 0;
  std::size_t moves = 0;

  Counts() = default;
  Counts(const Counts&) = delete ("Unused");
  Counts(Counts&&) = delete ("Unused");
  Counts& operator=(const Counts&) = delete ("Unused");
  Counts& operator=(Counts&&) = delete ("Unused");
  ~Counts() = default;
};

template <std::size_t PaddingSize>
class Tracker {
  Counts* counts_;
  int value_;
  [[maybe_unused]] std::array<std::byte, PaddingSize> padding_{};

 public:
  Tracker(Counts* counts, int value) noexcept
      : counts_(counts), value_(value) {}

  Tracker(const Tracker& other) : counts_(other.counts_), value_(other.value_) {
    ++counts_->copies;
  }

  Tracker(Tracker&& other) noexcept
      : counts_(other.counts_), value_(other.value_) {
    ++counts_->moves;
  }

  Tracker& operator=(const Tracker&) = delete ("Unused");
  Tracker& operator=(Tracker&&) = delete ("Unused");
  ~Tracker() = default;

  int value() const { return value_; }
};

using SmallTracker = Tracker<0>;
using LargeTracker = Tracker<detail::small_buffer_size>;

static_assert(detail::fits_in_small_buffer_v<SmallTracker, Alloc>);
static_assert(!detail::fits_in_small_buffer_v<LargeTracker, Alloc>);

// Throws when copied. `PaddingSize` decides whether it fits in the buffer.
template <std::size_t PaddingSize>
class ThrowingCopy {
  [[maybe_unused]] std::array<std::byte, PaddingSize> padding_{};

 public:
  ThrowingCopy() = default;

  ThrowingCopy(const ThrowingCopy&) { throw std::runtime_error("copy"); }

  ThrowingCopy(ThrowingCopy&&) noexcept = default;
  ThrowingCopy& operator=(const ThrowingCopy&) = delete ("Unused");
  ThrowingCopy& operator=(ThrowingCopy&&) = delete ("Unused");
  ~ThrowingCopy() = default;

  int value() const { return -1; }
};

struct LargeHolder {
  Small inner_;
  TooLargeForSmallBuffer padding_{};

  int value() const { return -2; }
};

struct SmallHolder {
  Small inner_;

  int value() const { return -3; }
};

static_assert(!detail::fits_in_small_buffer_v<LargeHolder, Alloc>);
static_assert(detail::fits_in_small_buffer_v<SmallHolder, Alloc>);

class MoveOnlyType {
  std::unique_ptr<int> pointer_;

 public:
  explicit MoveOnlyType(int value) : pointer_(std::make_unique<int>(value)) {}

  MoveOnlyType(const MoveOnlyType&) = delete ("Unused");
  MoveOnlyType(MoveOnlyType&&) = default;
  MoveOnlyType& operator=(const MoveOnlyType&) = delete ("Unused");
  MoveOnlyType& operator=(MoveOnlyType&&) = default;
  ~MoveOnlyType() = default;

  int value() const { return *pointer_; }
};

class OtherMoveOnlyType {
  std::unique_ptr<int> pointer_;

 public:
  explicit OtherMoveOnlyType(int value)
      : pointer_(std::make_unique<int>(value)) {}

  OtherMoveOnlyType(const OtherMoveOnlyType&) = delete ("Unused");
  OtherMoveOnlyType(OtherMoveOnlyType&&) = default;
  OtherMoveOnlyType& operator=(const OtherMoveOnlyType&) = delete ("Unused");
  OtherMoveOnlyType& operator=(OtherMoveOnlyType&&) = default;
  ~OtherMoveOnlyType() = default;

  int value() const { return *pointer_; }
};

TEST(ConvertingAssignmentTest, ReplacesObjectOfAnotherType) {
  protocol<Interface> p(Small{1});

  p = Large{2};

  EXPECT_EQ(p.value(), 2);
  EXPECT_EQ(target_type(p), typeid(Large));

  p = Small{3};

  EXPECT_EQ(p.value(), 3);
  EXPECT_EQ(target_type(p), typeid(Small));
}

TEST(ConvertingAssignmentTest, AssignsToValuelessProtocol) {
  protocol<Interface> p(Small{1});
  protocol<Interface> moved(std::move(p));

  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the test
  // assigns to a moved-from protocol on purpose.
  ASSERT_TRUE(valueless_after_move(p));

  p = Small{4};

  EXPECT_FALSE(valueless_after_move(p));
  EXPECT_EQ(p.value(), 4);
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ConvertingAssignmentTest, AssignsLvalueByCopy) {
  Counts counts;
  protocol<Interface> p(Small{1});
  LargeTracker large(&counts, 5);

  p = large;

  EXPECT_EQ(p.value(), 5);
  EXPECT_EQ(large.value(), 5);
  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(counts.moves, 0);

  Counts small_counts;
  SmallTracker small(&small_counts, 6);

  p = small;

  EXPECT_EQ(p.value(), 6);
  EXPECT_EQ(small.value(), 6);
  EXPECT_EQ(small_counts.copies, 1);
}

TEST(ConvertingAssignmentTest, AssignsMoveOnlyObjectToMoveOnlyInterface) {
  protocol<MoveOnlyInterface> p(MoveOnlyType(1));

  p = OtherMoveOnlyType(7);

  EXPECT_EQ(p.value(), 7);
  EXPECT_EQ(target_type(p), typeid(OtherMoveOnlyType));
}

TEST(ConvertingAssignmentTest, AssignsToImmovableInterface) {
  // Movable objects conform to an immovable interface, and assignment is the
  // only way such a protocol changes its object.
  protocol<ImmovableInterface> p(std::in_place_type<Small>, 1);

  p = Large{2};

  EXPECT_EQ(p.value(), 2);
  EXPECT_EQ(target_type(p), typeid(Large));
}

TEST(ConvertingAssignmentTest, ThrowingConstructionKeepsOldObject) {
  ThrowingCopy<0> small_throwing;
  ThrowingCopy<detail::small_buffer_size> large_throwing;

  protocol<Interface> small_old(Small{1});
  EXPECT_THROW(small_old = small_throwing, std::runtime_error);
  EXPECT_EQ(small_old.value(), 1);
  EXPECT_EQ(target_type(small_old), typeid(Small));

  protocol<Interface> large_old(Large{2});
  EXPECT_THROW(large_old = large_throwing, std::runtime_error);
  EXPECT_EQ(large_old.value(), 2);
  EXPECT_EQ(target_type(large_old), typeid(Large));
}

TEST(ConvertingAssignmentTest, AssignsFromObjectOwnedByHeldObject) {
  protocol<Interface> p(LargeHolder{Small{1}});

  p = protocol_cast<LargeHolder>(&p)->inner_;

  EXPECT_EQ(p.value(), 1);
  EXPECT_EQ(target_type(p), typeid(Small));

  p = LargeHolder{Small{2}};
  p = std::move(protocol_cast<LargeHolder>(&p)->inner_);

  EXPECT_EQ(p.value(), 2);
  EXPECT_EQ(target_type(p), typeid(Small));

  p = SmallHolder{Small{3}};
  p = protocol_cast<SmallHolder>(&p)->inner_;

  EXPECT_EQ(p.value(), 3);
  EXPECT_EQ(target_type(p), typeid(Small));

  p = SmallHolder{Small{4}};
  p = std::move(protocol_cast<SmallHolder>(&p)->inner_);

  EXPECT_EQ(p.value(), 4);
  EXPECT_EQ(target_type(p), typeid(Small));
}

TEST(ConvertingAssignmentTest, AllocatesThroughOwnAllocator) {
  unsigned allocations = 0;
  unsigned deallocations = 0;
  {
    protocol<Interface, Alloc> p(std::allocator_arg,
                                 Alloc(&allocations, &deallocations), Small{1});
    EXPECT_EQ(allocations, 0);

    p = Large{2};

    EXPECT_EQ(allocations, 1);
    EXPECT_EQ(deallocations, 0);

    p = Small{3};

    EXPECT_EQ(allocations, 1);
    EXPECT_EQ(deallocations, 1);

    p = Large{4};

    EXPECT_EQ(allocations, 2);
    EXPECT_EQ(deallocations, 1);
  }

  EXPECT_EQ(allocations, deallocations);
}

constexpr int AssignInConstantExpression() {
  protocol<Interface> p(Small{1});
  p = Small{2};
  const Large large{3};
  p = large;
  return protocol_cast<Large>(p).value_;
}

TEST(ConvertingAssignmentTest, UsableInConstantExpressions) {
  static_assert(AssignInConstantExpression() == 3);

  EXPECT_EQ(AssignInConstantExpression(), 3);
}

struct NonConforming {
  int other() const;
};

struct OtherInterface {
  int value() const;
  int other() const;
};

TEST(ConvertingAssignmentTest, RejectsNonConformingAndProtocolSources) {
  using Target = protocol<Interface>;

  static_assert(!std::is_assignable_v<Target&, int>);
  static_assert(!std::is_assignable_v<Target&, NonConforming>);
  static_assert(!std::is_assignable_v<Target&, protocol<OtherInterface>>);
  static_assert(!std::is_assignable_v<Target&, protocol_view<Interface>>);
  static_assert(!std::is_assignable_v<Target&, protocol<Interface, Alloc>>);
  static_assert(!std::is_assignable_v<Target&, MoveOnlyType>);

  static_assert(std::is_assignable_v<Target&, Small>);
  static_assert(std::is_assignable_v<Target&, Small&>);
  static_assert(std::is_assignable_v<Target&, const Small&>);
}

}  // namespace
}  // namespace xyz::tests

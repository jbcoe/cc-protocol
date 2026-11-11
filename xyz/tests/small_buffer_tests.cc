// Tests for protocol's small buffer optimization. Whether an object is in the
// small buffer is observed through allocation counts: an object in the buffer
// is not allocated.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <vector>

#include "xyz/protocol.hh"
#include "xyz/tests/tracking_allocator.h"

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
    ThrowingMove(const ThrowingMove&) = default;
    // NOLINTBEGIN(cppcoreguidelines-noexcept-move-operations,hicpp-noexcept-move,performance-noexcept-move-constructor):
    // the test needs a move constructor that can throw.
    ThrowingMove(ThrowingMove&&) noexcept(false) = default;
    // NOLINTEND(cppcoreguidelines-noexcept-move-operations,hicpp-noexcept-move,performance-noexcept-move-constructor)
    ThrowingMove& operator=(const ThrowingMove&) = default;
    ThrowingMove& operator=(ThrowingMove&&) = default;
    ~ThrowingMove() = default;
  };

  static_assert(!fits_in_small_buffer_v<ThrowingMove, Alloc>);
}

// Objects in the buffer are constructed and destroyed through the allocator,
// so a buffered object still receives an allocator that passes itself on.
// Relocation constructs through the allocator too, so an allocator whose
// `construct` can throw never uses the buffer.

TEST(SmallBufferTest, AllocatorAwareTypeFitsWithTheDefaultAllocator) {
  static_assert(
      fits_in_small_buffer_v<std::vector<int>, std::allocator<std::byte>>);
}

TEST(SmallBufferTest, NothingFitsWithAnAllocatorWhoseConstructCanThrow) {
  using PolymorphicAllocator = std::pmr::polymorphic_allocator<std::byte>;

  static_assert(!fits_in_small_buffer_v<SmallTracker, PolymorphicAllocator>);
}

// Counts the calls made to the `ConstructingAllocator`s that share it.
struct AllocatorCalls {
  unsigned allocations = 0;
  unsigned deallocations = 0;
  unsigned constructions = 0;
  unsigned destructions = 0;
};

// Constructs objects with uses-allocator construction, passing itself to
// those that take an allocator, as `std::pmr::polymorphic_allocator` does.
// Unlike it, `construct` is `noexcept` whenever the constructor it calls is.
template <typename T>
struct ConstructingAllocator {
  using value_type = T;

  AllocatorCalls* calls_;

  explicit ConstructingAllocator(AllocatorCalls* calls) noexcept
      : calls_(calls) {}

  template <typename U>
  ConstructingAllocator(const ConstructingAllocator<U>& other) noexcept
      : calls_(other.calls_) {}

  T* allocate(std::size_t n) {
    ++calls_->allocations;
    return std::allocator<T>{}.allocate(n);
  }

  void deallocate(T* pointer, std::size_t n) noexcept {
    ++calls_->deallocations;
    std::allocator<T>{}.deallocate(pointer, n);
  }

  template <typename U, typename... Args>
  static constexpr bool is_nothrow_constructible_using_allocator() {
    if constexpr (!std::uses_allocator_v<U, ConstructingAllocator>) {
      return std::is_nothrow_constructible_v<U, Args...>;
    } else if constexpr (std::is_constructible_v<U, std::allocator_arg_t,
                                                 const ConstructingAllocator&,
                                                 Args...>) {
      return std::is_nothrow_constructible_v<
          U, std::allocator_arg_t, const ConstructingAllocator&, Args...>;
    } else {
      return std::is_nothrow_constructible_v<U, Args...,
                                             const ConstructingAllocator&>;
    }
  }

  template <typename U, typename... Args>
  void construct(U* pointer, Args&&... args) noexcept(
      is_nothrow_constructible_using_allocator<U, Args...>()) {
    ++calls_->constructions;
    std::uninitialized_construct_using_allocator(pointer, *this,
                                                 std::forward<Args>(args)...);
  }

  template <typename U>
  void destroy(U* pointer) noexcept {
    ++calls_->destructions;
    std::destroy_at(pointer);
  }

  friend bool operator==(const ConstructingAllocator&,
                         const ConstructingAllocator&) = default;
};

using ConstructingProtocol =
    protocol<Interface, ConstructingAllocator<std::byte>>;

// Small and allocator-aware: `value` reports whether it received an
// allocator when it was constructed.
class AllocatorAware {
 public:
  using allocator_type = ConstructingAllocator<std::byte>;

  AllocatorAware() = default;

  AllocatorAware(std::allocator_arg_t, const allocator_type&) noexcept
      : received_allocator_(true) {}

  AllocatorAware(const AllocatorAware&) = default;

  AllocatorAware(std::allocator_arg_t, const allocator_type&,
                 const AllocatorAware&) noexcept
      : received_allocator_(true) {}

  AllocatorAware(AllocatorAware&&) noexcept = default;

  // NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved): there is
  // nothing to move; the constructor only records that it was given an
  // allocator.
  AllocatorAware(std::allocator_arg_t, const allocator_type&,
                 AllocatorAware&&) noexcept
      : received_allocator_(true) {}

  // NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved)

  AllocatorAware& operator=(const AllocatorAware&) = default;
  AllocatorAware& operator=(AllocatorAware&&) noexcept = default;
  ~AllocatorAware() = default;

  int value() const { return received_allocator_ ? 1 : 0; }

 private:
  bool received_allocator_ = false;
};

static_assert(
    fits_in_small_buffer_v<AllocatorAware, ConstructingAllocator<std::byte>>);

TEST(SmallBufferTest, BufferedAllocatorAwareObjectReceivesTheAllocator) {
  AllocatorCalls calls;
  ConstructingAllocator<std::byte> alloc(&calls);
  {
    ConstructingProtocol p(std::allocator_arg, alloc,
                           std::in_place_type<AllocatorAware>);
    EXPECT_EQ(p.value(), 1);

    ConstructingProtocol moved(std::move(p));
    EXPECT_EQ(moved.value(), 1);

    // NOLINTBEGIN(performance-unnecessary-copy-initialization): the test
    // checks copy construction.
    ConstructingProtocol copy(moved);
    // NOLINTEND(performance-unnecessary-copy-initialization)
    EXPECT_EQ(copy.value(), 1);
  }
  EXPECT_EQ(calls.allocations, 0);
  EXPECT_EQ(calls.constructions, calls.destructions);
}

// Every object the allocator constructs is destroyed through it, however
// often the protocols holding it are moved, swapped and assigned.
TEST(SmallBufferTest, BufferedObjectConstructionsAndDestructionsBalance) {
  AllocatorCalls calls;
  ConstructingAllocator<std::byte> alloc(&calls);
  Counts counts;
  {
    ConstructingProtocol p(std::allocator_arg, alloc,
                           std::in_place_type<SmallTracker>, &counts, 1);
    ConstructingProtocol q(std::allocator_arg, alloc,
                           std::in_place_type<SmallTracker>, &counts, 2);
    EXPECT_EQ(calls.constructions, 2);

    ConstructingProtocol moved(std::move(p));
    swap(moved, q);
    q = moved;
    moved = std::move(q);

    EXPECT_EQ(moved.value(), 2);
    EXPECT_EQ(calls.constructions - calls.destructions, 1);
  }
  EXPECT_EQ(calls.allocations, 0);
  EXPECT_EQ(calls.constructions, calls.destructions);
  EXPECT_EQ(counts.live, 0);
}

// ---------------------------------------------------------------------------
// Construction.
// ---------------------------------------------------------------------------

TEST(SmallBufferTest, SmallObjectIsNotAllocated) {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol p(std::allocator_arg, alloc, SmallTracker(&counts, 1));

  EXPECT_EQ(p.value(), 1);
  EXPECT_EQ(allocs, 0);
  EXPECT_EQ(counts.live, 1);
}

TEST(SmallBufferTest, LargeObjectIsAllocated) {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);
  {
    TrackingProtocol p(std::allocator_arg, alloc, LargeTracker(&counts, 1));

    EXPECT_EQ(p.value(), 1);
    EXPECT_EQ(allocs, 1);
  }
  EXPECT_EQ(deallocs, 1);
}

TEST(SmallBufferTest, InPlaceConstructionOfSmallObject) {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol p(std::allocator_arg, alloc,
                     std::in_place_type<SmallTracker>, &counts, 3);

  EXPECT_EQ(p.value(), 3);
  EXPECT_EQ(allocs, 0);
  EXPECT_EQ(counts.moves, 0);
}

// ---------------------------------------------------------------------------
// Copy and move construction.
// ---------------------------------------------------------------------------

template <typename T>
void check_copy_construction(unsigned expected_allocations) {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);
  {
    TrackingProtocol p(std::allocator_arg, alloc, std::in_place_type<T>,
                       &counts, 1);
    // NOLINTBEGIN(performance-unnecessary-copy-initialization): the test
    // checks copy construction.
    TrackingProtocol copy(p);
    // NOLINTEND(performance-unnecessary-copy-initialization)

    EXPECT_EQ(copy.value(), 1);
    EXPECT_EQ(p.value(), 1);
    EXPECT_EQ(counts.copies, 1);
    EXPECT_EQ(counts.live, 2);
    EXPECT_EQ(allocs, expected_allocations);
  }
  EXPECT_EQ(deallocs, expected_allocations);
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
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol p(std::allocator_arg, alloc, std::in_place_type<T>, &counts,
                     1);
  TrackingProtocol moved(std::move(p));

  EXPECT_EQ(moved.value(), 1);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test checks the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(p));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
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
  unsigned source_allocs = 0;
  unsigned source_deallocs = 0;
  Alloc source_alloc(&source_allocs, &source_deallocs);
  unsigned target_allocs = 0;
  unsigned target_deallocs = 0;
  Alloc target_alloc(&target_allocs, &target_deallocs);

  TrackingProtocol p(std::allocator_arg, source_alloc,
                     SmallTracker(&counts, 1));
  counts.moves = 0;
  TrackingProtocol moved(std::allocator_arg, target_alloc, std::move(p));

  EXPECT_EQ(moved.value(), 1);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test checks the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(p));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(counts.live, 1);
  EXPECT_EQ(source_allocs, 0);
  EXPECT_EQ(target_allocs, 0);
}

TEST(SmallBufferTest, MoveConstructLargeWithUnequalAllocator) {
  Counts counts;
  unsigned source_allocs = 0;
  unsigned source_deallocs = 0;
  Alloc source_alloc(&source_allocs, &source_deallocs);
  unsigned target_allocs = 0;
  unsigned target_deallocs = 0;
  Alloc target_alloc(&target_allocs, &target_deallocs);
  {
    TrackingProtocol p(std::allocator_arg, source_alloc,
                       LargeTracker(&counts, 1));
    counts.moves = 0;
    TrackingProtocol moved(std::allocator_arg, target_alloc, std::move(p));

    EXPECT_EQ(moved.value(), 1);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
    // test checks the moved-from state on purpose.
    EXPECT_TRUE(valueless_after_move(p));
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_EQ(counts.moves, 1);
    EXPECT_EQ(counts.live, 1);
    EXPECT_EQ(source_deallocs, 1);
    EXPECT_EQ(target_allocs, 1);
  }
  EXPECT_EQ(target_deallocs, 1);
}

// ---------------------------------------------------------------------------
// Copy and move assignment.
// ---------------------------------------------------------------------------

template <typename Target, typename Source>
void check_copy_assignment() {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol target(std::allocator_arg, alloc, std::in_place_type<Target>,
                          &counts, 1);
  TrackingProtocol source(std::allocator_arg, alloc, std::in_place_type<Source>,
                          &counts, 2);
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

// A small source object is moved twice: out of the source into temporary
// storage, which leaves the source valueless before the target's object is
// destroyed (the source may be owned by that object), and then into the
// target. A large one changes owner without being moved.
template <typename Target, typename Source>
void check_move_assignment(std::size_t expected_moves) {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol target(std::allocator_arg, alloc, std::in_place_type<Target>,
                          &counts, 1);
  TrackingProtocol source(std::allocator_arg, alloc, std::in_place_type<Source>,
                          &counts, 2);
  target = std::move(source);

  EXPECT_EQ(target.value(), 2);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test checks the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(source));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  EXPECT_EQ(counts.moves, expected_moves);
  EXPECT_EQ(counts.live, 1);
}

TEST(SmallBufferTest, MoveAssignSmallToSmall) {
  check_move_assignment<SmallTracker, SmallTracker>(2);
}

TEST(SmallBufferTest, MoveAssignLargeToSmall) {
  check_move_assignment<SmallTracker, LargeTracker>(0);
}

TEST(SmallBufferTest, MoveAssignSmallToLarge) {
  check_move_assignment<LargeTracker, SmallTracker>(2);
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
  unsigned source_allocs = 0;
  unsigned source_deallocs = 0;
  Alloc source_alloc(&source_allocs, &source_deallocs);
  unsigned target_allocs = 0;
  unsigned target_deallocs = 0;
  Alloc target_alloc(&target_allocs, &target_deallocs);
  {
    TrackingProtocol target(std::allocator_arg, target_alloc,
                            SmallTracker(&counts, 1));
    TrackingProtocol source(std::allocator_arg, source_alloc,
                            std::in_place_type<Source>, &counts, 2);
    counts.moves = 0;
    target = std::move(source);

    EXPECT_EQ(target.value(), 2);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
    // test checks the moved-from state on purpose.
    EXPECT_TRUE(valueless_after_move(source));
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
    EXPECT_EQ(counts.moves, expected_moves);
    EXPECT_EQ(counts.live, 1);
    EXPECT_EQ(target_allocs, expected_target_allocations);
    EXPECT_EQ(source_allocs, source_deallocs);
  }
  EXPECT_EQ(target_deallocs, expected_target_allocations);
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
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol left(std::allocator_arg, alloc, std::in_place_type<Left>,
                        &counts, 1);
  TrackingProtocol right(std::allocator_arg, alloc, std::in_place_type<Right>,
                         &counts, 2);
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

}  // namespace
}  // namespace xyz::tests

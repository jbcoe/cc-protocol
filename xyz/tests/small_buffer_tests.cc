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

One minus epsilon and one there exists a rational number or identity of rationals. That rational number is less than one, and it's going to be greater than zero, so it's also an S. So you have one element that you can say, I know exists in both sets. The neighborhood and S, and then you could do the same thing for between one and one plus epsilon for the second party you could use ten super rashes. You could use density of your rationals could you use the existence of like could you just use one minus epsilon of the Q you could it's gonna be yes you could pick a specific one like one hundred cents that is correct to be a service so kind of like I did when I argued three was I used three plus epsilon per two I said it's in the neighborhood and it's in so you can also pick a specific one that way as long as you define it based on the epsilon because you're letting epsilon be arbitrary so you can't just do plus half because that doesn't move when you change your epsilon so this is basically things that you would need to include in your proof you would also need to include that the choice of epsilon is arbitrary because you need every neighborhood once you intersect S and intersect let's look at this first one if S is a boundary interval in R with the usual metric so R intervals zero one was a bounded interval then the boundary of S contains two end points. Does that sound like it's a correct statement what about it says bounded in yes it says it's a bounded one balance yes it's not a union of family animals so if you have a union of boundary intervals if that changes and it's not like you're necessarily have a meaning two intervals to have four in points and you don't have the intervals overleft so it could be behind in it you know two of the end points go inside the interval all of a sudden you have to when you do so this is just on a singleton bounded if it's an unbounded if it's unbounded on one side if it's an interval that's bounded on one side but not mounted on the other you will get one end as infinity is never considered one boundary point end point is boundary point doesn't say boundary it should say boundary point I'm going to say you have the animal that goes from including or non including age doesn't matter whether you include or not include a, you know exactly one boundary point because infinity and minus infinity are not included values they can never be bound what do you think about the next state so this b of s is the settlable boundary points which should be true so it's maybe best so it's not saying it's always last zero the boundary of s is contained in s so we know that if I have zero to one then yes the boundary of s is equal to the elements zero and one and zero is an s and one is an s. So in this case the boundary is but it says it may be so it's not saying it always has to be by looking open interval that boundary is not so if I look at the open interval that tells us that the boundary may be in the comment so I'm going to think about the same kind of interval zero one the boundary of s zero one zero is in r sub minus s one is in r semines s so it may be true what do you think about this statement is it possible for the boundary was set to be the empty set of an example of the set words boundary empty set of already usual metrics and the number line what set on the number line would have a tenty found just the note down the pool set alls yeah so nice glue that would so it may be that it's empty that's not the only time to sensing but those are examples that we can actually grab on so that leaves us with our correct answer for which is false is that the boundary of always confused always countful can you say something something density pressures yes any examples for on the accountful boundary sets because I went through a couple of examples in my heads that meaning like they are rationals within the reals and stuff and like it sounds like I could find something that actually I couldn't find something that was dense and had boundaries so why does not why do the irrationals not a boundaries because of the well this is also where am I just be wrong you may be answered what we're talking about next okay so I was thinking that I'm trying to look for some neighborhood that centered on an irrational number any real number on any real number on any real number where it would like contain professionals and irrationals oh wait on does that work there you go back to the definition I need to ponder this so you actually sold across sets that have an uncountable numbers I think I messed up the message identified as not having reports when we talk about the next subject probably is what we were thinking so that is a good example of a set that has an uncountable number of elements because every real number is a value point q because any neighborhood so pick a real number C right neighborhood in that neighborhood between c miss epsilon c plus epsilon by density irrationals there exists irrational by density irrationals that you coexist in irrational so it intersects both the set and complement a set like an extra like for all somewhere in there that was not supposed to do there so then it's a good example of what is has a boundary that is uncountable that leads us to the definition of interior point and this is where interior point notice okay so this is what it is but not super happy about it mirrors the definition of vote point in that it says x is an element space but the x has to be in x in order for this truth so if you write x is in such that this stuff works I okay with that the only way a neighborhood centered and x could be indeed in s is that x is already known in the s, otherwise it's already broken so if you happen to write that I will let that one go there are often up there are times when I went this is not a recognition it has us to do it this way so we're talking about the interior instead of for all it's there exists epsilon where you can argue that the neighborhood I'm pointing with my fingers that you do with a later pointer the neighborhood centre and ninety degrees epsilon is totally contained an S and then we have this rotation which is awkward to right by the way in what's happening because it wants it to be an int just wants it to be integral or an into church and I don't want to be in so it's set S oh that's not another way to write sets here S take a point in S and you need to be able to find an epsilon so you could draw a neighborhood that's in S right if I try to do that for a point right here which is on the boundary whenever I draw my neighborhood I don't stay in S so where it's up so that's kind of like the difference looks kind of like right enough we have R with the usual metric this is probably going to help us visualize here's our set S is one or pi an interior point of the element zero the interval one third so that's going to be in here about two about three actually cool not three now I ask you for boundary points which one is boundary point the only one you can answer is the one I said it's not an interior point it's I mean I could also put four in here and you should be able to tell me four is not interior point of this set so it doesn't have to be specifically related to set if we want to show that p zero one is an interior point interval, this is my s zero one what is it that I need to show which of these things do I need to show so I want to find something that's longer than zero because it is very exists it's not for all so I don't want to do that there exists so I want to find some epsilon greater than zero and this one is the correct inconclusion that I need a neighborhood is contained in what epsilon will be possible to pick greece can you suggest an epsilon that would possibly work issues like so you basically show up one zero one in that dimension that works Charles can you suggest another epsilon that works also we can also just use zero point zero one because our neighborhoods don't include the boundaries so it's safe to use what was already given the point zero one it would be safe to use that as well why would that be true? Because zero point zero one is closer to zero kind of had this idea before point zero one is closer to zero so that's the distance to zero so the neighborhood, the biggest neighborhood I can create has that radius between zero one point zero one so radius to the biggest radius in use and oxygen neighborhood. I don't want to use zero point zero nine actually I don't want to use whatever it is zero point nine nine I didn't do that close to the other animals any positive value less than or equal to point zerowhin this oh no that's the interval and this is the neighborhood it's describing that neighborhood so that's why it looks kind of awkward and that neighborhood is wholly contained so you can argue that all the values are between zero and one weird is that what you just said what did you just said it was weird no I oh I think we already did this okay oh not weird okay so tighten is this first statement correct about the interior of the closed interval zero one is that the correct set for the interior place and what about this one for the open interval from zero to one so why is that not correct can it include points that aren't in x visits open try again because the interior points picture maybe that would help to visualize so I have zero to one like this no problem zero to one like this so interior points if I'm trying to draw them I'm still going to draw like this long does it make sense that those would potentially be in the same set so these two are correct we're not picking up extra anything like right it's just that it happens to be that the set of interior points on the part B is exactly the entire set, whereas in part A we're dropping off the boundary ones but the boundary points are the same for Part A and Part B zero and one they just don't aren't in the set for Part B Xluded already so if we exclude boundary points then you just get the an original path okay John what did we think about the interior points of the reals? Do you think that that is all real numbers yes okay what about the interior point of the naturals? Does it make sense that that's the empty set so I'm just going to draw a quick picture for that one if I try to say well this natural number is an interior point as soon as I create an interval around it I contain that non natural numbers in that interval so we can't be interior so these ones are also correct which means that the interior point of the rationals cannot be the rationals itself now that's the same kind of argument that we just had for why the boundary point of the rationals is every real number every neighborhood centered at a rational number contains both rationals and irrationals so you can't find an epsilon where the neighborhood is wholly only rationals you'll always an irrational will always speak in just like I said that this was the interior was empty we can argue that the boundary of the naturals is the naturals itself and we can argue that the boundary of the rationals is the rationals so that that statement be false so it has no matter and there's a reason why it makes sense that if I can argue the boundary is a lot set then we have something around this is a little bit more abstract but if we need to, we can think about real number one kind of exams Charlie I called your name forgotten to check if you were here for that do you think that it is true that the set of interior points is not empty always wait what is this is just a general statement we don't have a specific set right like from what we just talked about in our last example is this a true statement you could have a set note here just like I talked about the fact that the natural numbers have no interior points but what about the boundary you can also have a set of no boundary you have to set with no boundary points and our example that we talked about was the set itself the set of all reals so we can have no boundary points dawn what do you think about this no because you can have boundaries that aren't in the set is asking if the set is contained in the set of sorry I thought I thought you were looking at D oh no I was underlining C oh okay a subset of boundary points and interior so the union of the boundary points in the interior that it's a union of the boundary points and interior place it makes sense it's feasible okay well let's let's sit down you said you were going to make a comment about D so what do you think is true about Di was going to make a comment about D and that you could have boundary plays that are not in the set no wait and if they're not except that can't be interior ones right the only place like if it's if it's an interior point then it's already in the set and stuff around it is in the set if it's a boundary point you're not guaranteed you're in set so this one can't work so this one's false let's see this one we said the interior of N is equal to the empty set this one we said a boundary of R is equal to the empty set so those are false statements right we're trying to find the true one Don what do you think about this last statement can the set of boundary points intersect the set of interior points be on it? I don't think so because in order to no no they can't because in order to be a boundary point you need to have a neighborhood that contains a point that's doubt in the set it's not a neighborhood it's every neighbor every neighborhood and this requires a single neighborhood that doesn't contain points outside the cell but this one says every neighbor contains points in and out of the set so those two definitions do not make it's like oil and water they just don't mix so the correct statement here this is also false the boundary set and interior sets are disjoint their intersections is false this is false and this one is the true statements have to either be interior points around I took all that time to write the silico
// Relocation goes through the allocator the object was stored with: moving a
// protocol constructs the moved object, and destroys the original, through it.
TEST(SmallBufferTest, MoveRelocatesThroughTheAllocator) {
  AllocatorCalls calls;
  ConstructingAllocator<std::byte> alloc(&calls);
  Counts counts;
  {
    ConstructingProtocol p(std::allocator_arg, alloc,
                           std::in_place_type<SmallTracker>, &counts, 1);
    ConstructingProtocol moved(std::move(p));

    EXPECT_EQ(moved.value(), 1);
    EXPECT_EQ(calls.constructions, 2);
    EXPECT_EQ(calls.destructions, 1);
  }
  EXPECT_EQ(calls.destructions, 2);
}

// As `ConstructingAllocator`, but propagated on swap, so that protocols with
// unequal allocators can be swapped.
template <typename T>
struct SwappedConstructingAllocator : ConstructingAllocator<T> {
  using propagate_on_container_swap = std::true_type;

  using ConstructingAllocator<T>::ConstructingAllocator;

  template <typename U>
  SwappedConstructingAllocator(
      const SwappedConstructingAllocator<U>& other) noexcept
      : ConstructingAllocator<T>(other) {}

  template <typename Other>
  struct rebind {
    using other = SwappedConstructingAllocator<Other>;
  };
};

// Swapping relocates each object through the allocator it was stored with,
// which then moves with it.
TEST(SmallBufferTest, SwapRelocatesEachObjectThroughItsOwnAllocator) {
  using SwappedAllocator = SwappedConstructingAllocator<std::byte>;
  using SwappedProtocol = protocol<Interface, SwappedAllocator>;

  AllocatorCalls left_calls;
  AllocatorCalls right_calls;
  Counts counts;
  {
    SwappedProtocol left(std::allocator_arg, SwappedAllocator(&left_calls),
                         std::in_place_type<SmallTracker>, &counts, 1);
    SwappedProtocol right(std::allocator_arg, SwappedAllocator(&right_calls),
                          std::in_place_type<SmallTracker>, &counts, 2);
    swap(left, right);

    EXPECT_EQ(left.value(), 2);
    EXPECT_EQ(right.value(), 1);
    EXPECT_EQ(left.get_allocator(), SwappedAllocator(&right_calls));
    EXPECT_EQ(right.get_allocator(), SwappedAllocator(&left_calls));
    // `left`'s object is relocated into temporary storage and then into
    // `right`; `right`'s object is relocated into `left`.
    EXPECT_EQ(left_calls.constructions, 3);
    EXPECT_EQ(left_calls.destructions, 2);
    EXPECT_EQ(right_calls.constructions, 2);
    EXPECT_EQ(right_calls.destructions, 1);
  }
  EXPECT_EQ(left_calls.constructions, left_calls.destructions);
  EXPECT_EQ(right_calls.constructions, right_calls.destructions);
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

// Swapping a protocol with itself must not relocate its object: relocating
// out of storage that was just emptied would move from an object that is not
// there.
TEST(SmallBufferTest, SelfSwapOfSmallObjectMovesNothing) {
  Counts counts;
  unsigned allocs = 0;
  unsigned deallocs = 0;
  Alloc alloc(&allocs, &deallocs);

  TrackingProtocol p(std::allocator_arg, alloc,
                     std::in_place_type<SmallTracker>, &counts, 1);
  swap(p, p);

  EXPECT_EQ(p.value(), 1);
  EXPECT_EQ(counts.moves, 0);
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

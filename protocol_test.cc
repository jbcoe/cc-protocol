// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view: type traits, special member functions, constructability,
// protocol_cast and target_type.

#include "protocol.hh"

#include <gtest/gtest.h>

#include <concepts>
#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using xyz::reflection::is_protocol_v;
using xyz::reflection::is_protocol_view_v;
using xyz::reflection::is_valid_interface;
using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// ---------------------------------------------------------------------------
// Type trait tests.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, IsProtocolV) {
  struct Interface {};

  static_assert(is_protocol_v<protocol<Interface>>);
  static_assert(!is_protocol_v<protocol_view<Interface>>);
  static_assert(!is_protocol_v<Interface>);
}

TEST(ReflectionProtocolViewTest, IsProtocolViewV) {
  struct Interface {};

  static_assert(is_protocol_view_v<protocol_view<Interface>>);
  static_assert(is_protocol_view_v<protocol_view<const Interface>>);
  static_assert(!is_protocol_view_v<protocol<Interface>>);
  static_assert(!is_protocol_view_v<Interface>);
}

TEST(ReflectionProtocolTest, IsValidInterface) {
  static_assert(!is_valid_interface<int>);

  struct Invalid1 {
    int foo() volatile;
  };

  static_assert(!is_valid_interface<Invalid1>);

  union Invalid2 {
    int foo();
  };

  static_assert(!is_valid_interface<Invalid2>);

  struct Valid {
    int foo();
  };

  static_assert(is_valid_interface<Valid>);
  static_assert(!is_valid_interface<Valid&>);
  static_assert(!is_valid_interface<const Valid>);
}

// ---------------------------------------------------------------------------
// Special member function tests.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolViewTest, CheckSpecialMembers) {
  // `protocol_view` is not default-constructible but can be copied, moved,
  // assigned, move assigned and destroyed.
  struct A {};

  static_assert(!std::is_default_constructible_v<protocol_view<A>>);
  static_assert(std::is_copy_constructible_v<protocol_view<A>>);
  static_assert(std::is_move_constructible_v<protocol_view<A>>);
  static_assert(std::is_copy_assignable_v<protocol_view<A>>);
  static_assert(std::is_move_assignable_v<protocol_view<A>>);
  static_assert(std::is_destructible_v<protocol_view<A>>);

  static_assert(!std::is_default_constructible_v<protocol_view<const A>>);
  static_assert(std::is_copy_constructible_v<protocol_view<const A>>);
  static_assert(std::is_move_constructible_v<protocol_view<const A>>);
  static_assert(std::is_copy_assignable_v<protocol_view<const A>>);
  static_assert(std::is_move_assignable_v<protocol_view<const A>>);
  static_assert(std::is_destructible_v<protocol_view<const A>>);
}

TEST(ReflectionProtocolViewTest,
     CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // `protocol_view`'s special member functions do not depend on those of the
  // viewed type.
  struct D {
    D() = delete;
    D(const D&) = delete;
    D(D&&) = delete;
    D& operator=(const D&) = delete;
    D& operator=(D&&) = delete;
    ~D() = delete;
  };

  static_assert(!std::is_default_constructible_v<protocol_view<D>>);
  static_assert(std::is_copy_constructible_v<protocol_view<D>>);
  static_assert(std::is_move_constructible_v<protocol_view<D>>);
  static_assert(std::is_copy_assignable_v<protocol_view<D>>);
  static_assert(std::is_move_assignable_v<protocol_view<D>>);
  static_assert(std::is_destructible_v<protocol_view<D>>);
}

TEST(ReflectionProtocolTest, CheckSpecialMembers) {
  // `protocol` is not default-constructible but can be copied, moved,
  // assigned and move assigned if the underlying type can be copied.
  struct A {};

  static_assert(!std::is_default_constructible_v<protocol<A>>);
  static_assert(std::is_copy_constructible_v<protocol<A>>);
  static_assert(std::is_move_constructible_v<protocol<A>>);
  static_assert(std::is_copy_assignable_v<protocol<A>>);
  static_assert(std::is_move_assignable_v<protocol<A>>);
}

TEST(ReflectionProtocolTest,
     CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // `protocol` is not default-constructible and is not move or
  // copy constructible or assignable.
  struct D {
    D() = delete;
    D(const D&) = delete;
    D(D&&) = delete;
    D& operator=(const D&) = delete;
    D& operator=(D&&) = delete;
    ~D() = delete;
  };

  static_assert(!std::is_default_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_constructible_v<protocol<D>>);
  static_assert(!std::is_move_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_assignable_v<protocol<D>>);
  static_assert(!std::is_move_assignable_v<protocol<D>>);
}

TEST(ReflectionProtocolViewTest, IsTriviallyCopyable) {
  struct A {
    int get() const;
    void set(int);
  };

  static_assert(std::is_trivially_copyable_v<protocol_view<A>>);
  static_assert(std::is_trivially_copy_constructible_v<protocol_view<A>>);
  static_assert(std::is_trivially_move_constructible_v<protocol_view<A>>);
  static_assert(std::is_trivially_copy_assignable_v<protocol_view<A>>);
  static_assert(std::is_trivially_move_assignable_v<protocol_view<A>>);
  static_assert(std::is_trivially_destructible_v<protocol_view<A>>);
  static_assert(std::is_nothrow_copy_constructible_v<protocol_view<A>>);
  static_assert(std::is_nothrow_move_constructible_v<protocol_view<A>>);
  static_assert(std::is_nothrow_copy_assignable_v<protocol_view<A>>);
  static_assert(std::is_nothrow_move_assignable_v<protocol_view<A>>);

  static_assert(std::is_trivially_copyable_v<protocol_view<const A>>);
  static_assert(std::is_trivially_copy_constructible_v<protocol_view<const A>>);
  static_assert(std::is_trivially_move_constructible_v<protocol_view<const A>>);
  static_assert(std::is_trivially_copy_assignable_v<protocol_view<const A>>);
  static_assert(std::is_trivially_move_assignable_v<protocol_view<const A>>);
  static_assert(std::is_trivially_destructible_v<protocol_view<const A>>);
  static_assert(std::is_nothrow_copy_constructible_v<protocol_view<const A>>);
  static_assert(std::is_nothrow_move_constructible_v<protocol_view<const A>>);
  static_assert(std::is_nothrow_copy_assignable_v<protocol_view<const A>>);
  static_assert(std::is_nothrow_move_assignable_v<protocol_view<const A>>);
}

TEST(ReflectionProtocolViewTest, MemberThunksCannotBeDetached) {
  struct A {
    int get() const;
    void set(int);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  Conforming c;
  protocol_view<A> view(c);

  static_assert(!std::is_copy_constructible_v<decltype(view.get)>);
  static_assert(!std::is_move_constructible_v<decltype(view.get)>);
  static_assert(!std::is_copy_assignable_v<decltype(view.get)>);
  static_assert(!std::is_move_assignable_v<decltype(view.get)>);
  static_assert(!std::is_default_constructible_v<decltype(view.get)>);
  static_assert(!std::is_destructible_v<decltype(view.get)>);
  static_assert(std::is_trivially_copyable_v<decltype(view.get)>);

  static_assert(!std::is_copy_constructible_v<decltype(view.set)>);
  static_assert(!std::is_move_constructible_v<decltype(view.set)>);
  static_assert(!std::is_copy_assignable_v<decltype(view.set)>);
  static_assert(!std::is_move_assignable_v<decltype(view.set)>);
  static_assert(!std::is_default_constructible_v<decltype(view.set)>);
  static_assert(!std::is_destructible_v<decltype(view.set)>);
  static_assert(std::is_trivially_copyable_v<decltype(view.set)>);

  view.set(7);
  const auto& get = view.get;
  EXPECT_EQ(get(), 7);
}

TEST(ReflectionProtocolTest, MemberThunksCannotBeDetached) {
  struct A {
    int get() const;
    void set(int);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  protocol<A> p(Conforming{});

  static_assert(!std::is_copy_constructible_v<decltype(p.get)>);
  static_assert(!std::is_move_constructible_v<decltype(p.get)>);
  static_assert(!std::is_copy_assignable_v<decltype(p.get)>);
  static_assert(!std::is_move_assignable_v<decltype(p.get)>);
  static_assert(!std::is_default_constructible_v<decltype(p.get)>);
  static_assert(!std::is_destructible_v<decltype(p.get)>);
  static_assert(std::is_trivially_copyable_v<decltype(p.get)>);

  static_assert(!std::is_copy_constructible_v<decltype(p.set)>);
  static_assert(!std::is_move_constructible_v<decltype(p.set)>);
  static_assert(!std::is_copy_assignable_v<decltype(p.set)>);
  static_assert(!std::is_move_assignable_v<decltype(p.set)>);
  static_assert(!std::is_default_constructible_v<decltype(p.set)>);
  static_assert(!std::is_destructible_v<decltype(p.set)>);
  static_assert(std::is_trivially_copyable_v<decltype(p.set)>);

  p.set(7);
  const auto& get = p.get;
  EXPECT_EQ(get(), 7);
}

TEST(ReflectionProtocolViewTest, CopiedViewCallsThroughToViewedObject) {
  struct A {
    int get() const;
    void set(int);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  Conforming c;
  protocol_view<A> view(c);

  protocol_view<A> copy_constructed(view);
  copy_constructed.set(1);
  EXPECT_EQ(view.get(), 1);
  EXPECT_EQ(c.value, 1);

  // NOLINTBEGIN(performance-move-const-arg,hicpp-move-const-arg): the test
  // shows that moving a trivially copyable view is equivalent to copying it.
  protocol_view<A> move_constructed(std::move(copy_constructed));
  // NOLINTEND(performance-move-const-arg,hicpp-move-const-arg)
  move_constructed.set(2);
  EXPECT_EQ(view.get(), 2);
  EXPECT_EQ(c.value, 2);

  Conforming other;
  protocol_view<A> other_view(other);
  other_view = view;
  other_view.set(3);
  EXPECT_EQ(view.get(), 3);
  EXPECT_EQ(c.value, 3);
  EXPECT_EQ(other.value, 0);

  Conforming yet_another;
  protocol_view<A> yet_another_view(yet_another);
  // NOLINTBEGIN(performance-move-const-arg,hicpp-move-const-arg): the test
  // shows that moving a trivially copyable view is equivalent to copying it.
  yet_another_view = std::move(other_view);
  // NOLINTEND(performance-move-const-arg,hicpp-move-const-arg)
  yet_another_view.set(4);
  EXPECT_EQ(view.get(), 4);
  EXPECT_EQ(c.value, 4);
  EXPECT_EQ(yet_another.value, 0);
}

TEST(ReflectionProtocolTest, CopiesAreIndependentObjects) {
  struct A {
    int get() const;
    void set(int);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  protocol<A> original(Conforming{});
  original.set(1);

  protocol<A> copy_constructed(original);
  copy_constructed.set(2);
  EXPECT_EQ(original.get(), 1);
  EXPECT_EQ(copy_constructed.get(), 2);

  protocol<A> move_constructed(std::move(copy_constructed));
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(copy_constructed));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  EXPECT_EQ(move_constructed.get(), 2);

  protocol<A> copy_assigned(Conforming{});
  copy_assigned = original;
  copy_assigned.set(3);
  EXPECT_EQ(original.get(), 1);
  EXPECT_EQ(copy_assigned.get(), 3);

  protocol<A> move_assigned(Conforming{});
  move_assigned = std::move(copy_assigned);
  EXPECT_EQ(move_assigned.get(), 3);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(copy_assigned));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolTest, SelfAssignmentLeavesValueUnchanged) {
  struct A {
    int get() const;
    void set(int);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  protocol<A> p(Conforming{});
  p.set(7);

  // Self-assignment through a reference, so the operators' self checks are
  // exercised rather than rejected at the call site.
  protocol<A>& same = p;
  p = same;
  EXPECT_EQ(p.get(), 7);
  EXPECT_FALSE(valueless_after_move(p));

  p = std::move(same);
  EXPECT_EQ(p.get(), 7);
  EXPECT_FALSE(valueless_after_move(p));
}

// ---------------------------------------------------------------------------
// Constructability tests.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, IsConstructibleFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol<Interface>, Conforming>);
  static_assert(!std::is_constructible_v<protocol<Interface>, NonConforming>);
}

TEST(ReflectionProtocolTest, IsConstructibleFromMoveOnlyType) {
  struct Interface {
    Interface(const Interface&) = delete;
    Interface(Interface&&) = delete;

    Interface& operator=(const Interface&) = delete;
    Interface& operator=(Interface&&) = delete;

    ~Interface() = default;
  };

  static_assert(!std::is_move_constructible_v<protocol<Interface>>);
  static_assert(!std::is_move_assignable_v<protocol<Interface>>);

  struct Default {};

  static_assert(std::is_constructible_v<protocol<Interface>, Default>);

  struct MoveOnly {
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) = default;

    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&&) = default;

    ~MoveOnly() = default;
  };

  static_assert(std::is_constructible_v<protocol<Interface>, MoveOnly>);

  struct Immovable {
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;

    Immovable& operator=(const Immovable&) = delete;
    Immovable& operator=(Immovable&&) = delete;

    ~Immovable() = default;
  };

  static_assert(std::is_constructible_v<protocol<Interface>, Immovable>);
}

TEST(ReflectionProtocolViewTest, IsConstructibleFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  // protocol_view's constructor takes U&, so constructibility is checked
  // from an lvalue, not a prvalue.
  static_assert(std::is_constructible_v<protocol_view<Interface>, Conforming&>);
  static_assert(std::is_convertible_v<Conforming&, protocol_view<Interface>>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, NonConforming&>);
  // A view of a temporary would dangle.
  static_assert(!std::is_constructible_v<protocol_view<Interface>, Conforming>);
}

TEST(ReflectionProtocolViewTest, NotConstructibleFromConstObject) {
  // protocol_view rejects a const object unconditionally: even though
  // Interface has no non-const methods (so a const object would actually
  // be safe to dispatch through), construction from one is still rejected,
  // because the rule doesn't inspect Interface at all.
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int get() const { return 0; }
  };

  static_assert(std::is_constructible_v<protocol_view<Interface>, Conforming&>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, const Conforming&>);
}

TEST(ReflectionProtocolViewTest, ConstViewIsConstructibleFromConstObject) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol_view<const Interface>,
                                        const Conforming&>);
  static_assert(
      std::is_constructible_v<protocol_view<const Interface>, Conforming&>);
  static_assert(
      std::is_convertible_v<const Conforming&, protocol_view<const Interface>>);
  static_assert(
      std::is_convertible_v<Conforming&, protocol_view<const Interface>>);
  static_assert(!std::is_constructible_v<protocol_view<const Interface>,
                                         const NonConforming&>);
  // A view of a temporary would dangle.
  static_assert(
      !std::is_constructible_v<protocol_view<const Interface>, Conforming>);
  static_assert(!std::is_constructible_v<protocol_view<const Interface>,
                                         const Conforming>);
}

TEST(ReflectionProtocolTest, IsConstructibleInPlaceFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol<Interface>,
                                        std::in_place_type_t<Conforming>>);
  static_assert(!std::is_constructible_v<protocol<Interface>,
                                         std::in_place_type_t<NonConforming>>);
}

TEST(ReflectionProtocolTest, IsConstructibleInPlaceWithArguments) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    explicit Conforming(std::string_view value);
    std::string_view name() const noexcept;
  };

  static_assert(std::is_constructible_v<protocol<Interface>,
                                        std::in_place_type_t<Conforming>,
                                        std::string_view>);
}

TEST(ReflectionProtocolTest, ProtocolCast) {
  struct Interface {};

  struct Conforming {
    int value;
  };

  protocol<Interface> p(Conforming{.value = 25});
  EXPECT_EQ(protocol_cast<Conforming>(p).value, 25);
  EXPECT_EQ(protocol_cast<Conforming>(&p)->value, 25);

  protocol_view<Interface> pv(p);
  EXPECT_EQ(protocol_cast<Conforming>(pv).value, 25);
  EXPECT_EQ(protocol_cast<Conforming>(&pv)->value, 25);
}

TEST(ReflectionProtocolTest, FailedProtocolCast) {
  struct Interface {};

  struct Conforming {
    int value;
  };

  struct OtherType {};

  protocol<Interface> p(Conforming{.value = 25});
  EXPECT_THROW(protocol_cast<OtherType>(p), xyz::reflection::bad_protocol_cast);
  EXPECT_EQ(protocol_cast<OtherType>(&p), nullptr);

  protocol_view<Interface> pv(p);
  EXPECT_THROW(protocol_cast<OtherType>(pv),
               xyz::reflection::bad_protocol_cast);
  EXPECT_EQ(protocol_cast<OtherType>(&pv), nullptr);
}

TEST(ReflectionProtocolViewTest, ConstProtocolCast) {
  struct Interface {};

  struct Conforming {
    int value;
  };

  Conforming c{.value = 20};
  protocol_view<const Interface> cv(c);

  // NOLINTBEGIN(readability-qualified-auto): the test demonstrates that
  // the type of auto is indeed const; adding const would not exercise the
  // behavior.
  auto& underlying = protocol_cast<Conforming>(cv);
  // NOLINTEND(readability-qualified-auto)
  static_assert(std::same_as<decltype(underlying), const Conforming&>);

  // NOLINTBEGIN(readability-qualified-auto): the test demonstrates that
  // the type of auto is indeed const; adding const would not exercise the
  // behavior.
  // NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): the type of underlying_ptr
  // is checked in the following line.
  auto* underlying_ptr = protocol_cast<Conforming>(&cv);
  // NOLINTEND(clang-analyzer-deadcode.DeadStores)
  // NOLINTEND(readability-qualified-auto)
  static_assert(std::same_as<decltype(underlying_ptr), const Conforming*>);
}

TEST(ReflectionProtocolViewTest, FailedConstProtocolCast) {
  struct Interface {};

  struct Conforming {};

  struct OtherType {};

  Conforming c;
  protocol_view<const Interface> pv(c);

  EXPECT_THROW(protocol_cast<OtherType>(pv),
               xyz::reflection::bad_protocol_cast);
  EXPECT_EQ(protocol_cast<OtherType>(&pv), nullptr);
}

TEST(ReflectionProtocolTest, ProtocolCastCopies) {
  struct Interface {
    Interface(const Interface&) = delete;
    Interface(Interface&&) = default;
    Interface& operator=(const Interface&) = delete;
    Interface& operator=(Interface&&) = delete;
    ~Interface() = default;
  };

  struct Conforming {
    int* copies = nullptr;

    Conforming(int& copyCounter) : copies(&copyCounter) {}

    Conforming(const Conforming& other) : copies(other.copies) { ++(*copies); }

    Conforming& operator=(const Conforming&) = default;

    Conforming(Conforming&&) = default;
    Conforming& operator=(Conforming&&) = default;

    ~Conforming() = default;
  };

  int copies{};

  protocol<Interface> p(Conforming{copies});

  // NOLINTBEGIN(clang-analyzer-deadcode.DeadStores): The test intentionally
  // creates unused copies.
  auto& _ = protocol_cast<Conforming&>(p);
  EXPECT_EQ(copies, 0);

  auto _ = protocol_cast<Conforming&>(p);
  EXPECT_EQ(copies, 1);

  auto _ = protocol_cast<Conforming>(p);
  EXPECT_EQ(copies, 2);

  auto _ = protocol_cast<Conforming&&>(std::move(p));
  EXPECT_EQ(copies, 2);
  // NOLINTEND(clang-analyzer-deadcode.DeadStores)
}

TEST(ReflectionProtocolTest, CatchingBadProtocolCast) {
  struct Interface {};

  struct Conforming {};

  struct OtherType {};

  protocol<Interface> p(Conforming{});

  try {
    protocol_cast<OtherType>(p);
  } catch (const std::exception& e) {
    EXPECT_EQ(std::string{e.what()}, "bad protocol_cast");
  }
}

TEST(ReflectionProtocolTest, TargetType) {
  struct Interface {};

  struct Conforming {};

  struct OtherType {};

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(target_type(p), typeid(Conforming));

  p = protocol<Interface>(OtherType{});
  EXPECT_EQ(target_type(p), typeid(OtherType));

  auto _ = std::move(p);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the test
  // exercises the moved-from state on purpose.
  EXPECT_EQ(target_type(p), typeid(void));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolViewTest, MutableTargetType) {
  struct Interface {};

  struct Conforming {};

  struct OtherType {};

  Conforming c{};
  protocol_view<Interface> p(c);
  EXPECT_EQ(target_type(p), typeid(Conforming));

  OtherType o{};
  p = o;
  EXPECT_EQ(target_type(p), typeid(OtherType));
}

TEST(ReflectionProtocolViewTest, ConstTargetType) {
  struct Interface {};

  struct Conforming {};

  struct OtherType {};

  Conforming c{};
  protocol_view<const Interface> p(c);
  EXPECT_EQ(target_type(p), typeid(Conforming));

  OtherType o{};
  p = o;
  EXPECT_EQ(target_type(p), typeid(OtherType));
}

TEST(ReflectionProtocolViewTest, MovedFromTargetType) {
  struct Interface {};

  struct Conforming {};

  struct OtherType {};

  protocol<Interface> movedFrom{Conforming{}};
  auto _ = std::move(movedFrom);

  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the test
  // exercises the moved-from state on purpose.
  protocol_view<Interface> pv(movedFrom);
  EXPECT_EQ(target_type(pv), typeid(void));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolTest, ValuelessAfterMoveFunctionDoesNotCollide) {
  struct Interface {
    bool valueless_after_move() const noexcept;
  };

  struct Conforming {
    bool was_moved_from_ = false;

    Conforming() = default;

    Conforming(const Conforming&) = default;

    Conforming(Conforming&& c) noexcept { c.was_moved_from_ = true; }

    Conforming& operator=(const Conforming&) = default;

    Conforming& operator=(Conforming&& c) noexcept {
      c.was_moved_from_ = false;
      return *this;
    };

    ~Conforming() = default;

    bool valueless_after_move() const noexcept { return was_moved_from_; }
  };

  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.

  // Check that the `valueless_after_move` member function works as expected.
  Conforming c;
  EXPECT_FALSE(c.valueless_after_move());
  [[maybe_unused]] auto cc = std::move(c);
  EXPECT_TRUE(c.valueless_after_move());

  // Free and member `valueless_after_move` on a `protocol`.
  protocol<Interface> p(Conforming{});
  EXPECT_FALSE(p.valueless_after_move());
  EXPECT_FALSE(valueless_after_move(p));
  auto pp = std::move(p);
  EXPECT_TRUE(valueless_after_move(p));

#if (defined(_MSC_VER) && defined(_DEBUG)) || (!defined(NDEBUG))
  EXPECT_DEATH(p.valueless_after_move(),
               "cannot call member function of valueless protocol");
#endif

  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

}  // namespace

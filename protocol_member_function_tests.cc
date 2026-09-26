// Tests for member function calls through protocol.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "protocol.hh"
#include "tracking_allocator.h"

using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// Concepts for negative member function tests: a requires-expression naming a
// member that does not exist is only a substitution failure in a template.
template <typename P>
concept has_update = requires(P& p) { p.update(0); };

template <typename P>
concept has_get_int = requires(P& p) { p.get(0); };

template <typename P>
concept has_get = requires(P& p) { p.get(); };

// Concept for testing a reference-qualified get().
template <typename P>
concept has_ref_get = requires(P p) { std::forward<P>(p).get(); };

// Member function forwarding tests for protocol.

TEST(ReflectionProtocolTest, ConstMemberFunction) {
  struct Interface {
    int get_value() const;
  };

  struct Conforming {
    int get_value() const { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.get_value(), 42);
}

TEST(ReflectionProtocolTest, NonConstMemberFunctionNotInvocableFromConst) {
  struct Interface {
    void update(int value);
  };

  // `protocol` propagates const: a const protocol exposes only the const
  // member functions of the interface.
  static_assert(
      !std::is_invocable_v<
          decltype((std::declval<const protocol<Interface>&>().update)), int>);
  static_assert(has_update<protocol<Interface>>);
  static_assert(!has_update<const protocol<Interface>>);
}

TEST(ReflectionProtocolTest, LvalueInterface) {
  struct Interface {
    int get() &;
  };

  static_assert(has_ref_get<protocol<Interface>&>);
  static_assert(!has_ref_get<protocol<Interface>&&>);

  static_assert(has_ref_get<protocol_view<Interface>&>);
  static_assert(has_ref_get<protocol_view<Interface>&&>);

  static_assert(!has_ref_get<protocol_view<const Interface>&>);
  static_assert(!has_ref_get<protocol_view<const Interface>&&>);
}

TEST(ReflectionProtocolTest, RvalueInterface) {
  struct Interface {
    int get() &&;
  };

  static_assert(!has_ref_get<protocol<Interface>&>);
  static_assert(has_ref_get<protocol<Interface>&&>);

  static_assert(!has_ref_get<protocol_view<Interface>&>);
  static_assert(!has_ref_get<protocol_view<Interface>&&>);

  static_assert(!has_ref_get<protocol_view<const Interface>&>);
  static_assert(!has_ref_get<protocol_view<const Interface>&&>);
}

TEST(ReflectionProtocolTest, SingleParameterMemberFunction) {
  struct Interface {
    void update(int value);
    int get() const;
  };

  struct Conforming {
    int last_value = 0;

    void update(int value) { last_value = value; }

    int get() const { return last_value; }
  };

  protocol<Interface> p(Conforming{});
  p.update(42);
  EXPECT_EQ(p.get(), 42);
  static_assert(!noexcept(p.update(1)));
}

TEST(ReflectionProtocolTest, NoexceptMemberFunction) {
  struct Interface {
    double compute(double input) noexcept;
  };

  struct Conforming {
    double compute(double input) noexcept { return input * 2.0; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.compute(21.0), 42.0);
  static_assert(noexcept(p.compute(1.0)));
}

TEST(ReflectionProtocolTest, MultiParameterMemberFunction) {
  struct Interface {
    int add(int a, int b) const;
  };

  struct Conforming {
    int add(int a, int b) const { return a + b; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.add(1, 2), 3);
}

TEST(ReflectionProtocolTest, VoidMemberFunction) {
  struct Interface {
    void reset();
    bool was_reset() const;
  };

  struct Conforming {
    bool reset_flag = false;

    void reset() { reset_flag = true; }

    bool was_reset() const { return reset_flag; }
  };

  protocol<Interface> p(Conforming{});
  p.reset();
  EXPECT_TRUE(p.was_reset());
}

TEST(ReflectionProtocolTest, MultipleMemberFunctions) {
  struct Interface {
    double add(double x, double y) const noexcept;
    double multiply(double x, double y) const noexcept;
  };

  struct Conforming {
    double add(double x, double y) const noexcept { return x + y; }

    double multiply(double x, double y) const noexcept { return x * y; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.add(1.0, 2.0), 3.0);
  EXPECT_EQ(p.multiply(3.0, 4.0), 12.0);
}

TEST(ReflectionProtocolTest, MixedConstAndMutatingMemberFunctions) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.get(), 0);
  p.set(7);
  EXPECT_EQ(p.get(), 7);
}

TEST(ReflectionProtocolTest, StaticMemberFunction) {
  struct Interface {
    int value() const;
  };

  struct Conforming {
    static int value() { return 42; }
  };

  protocol<Interface> p(std::in_place_type<Conforming>);
  EXPECT_EQ(p.value(), 42);
}

TEST(ReflectionProtocolTest, StaticMemberFunctionSatisfiesNonConstMember) {
  struct Interface {
    int next();
  };

  struct Conforming {
    static int next() {
      static int counter = 0;
      return ++counter;
    }
  };

  protocol<Interface> p(std::in_place_type<Conforming>);
  EXPECT_EQ(p.next(), 1);
  EXPECT_EQ(p.next(), 2);
}

TEST(ReflectionProtocolTest, ForwardingAfterCopyConstruction) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  protocol<Interface> a(Conforming{});
  a.set(1);
  // NOLINTBEGIN(performance-unnecessary-copy-initialization): the test
  // exercises copy construction on purpose.
  protocol<Interface> b(a);
  // NOLINTEND(performance-unnecessary-copy-initialization)
  b.set(2);

  // protocol owns a copy of the underlying object, so copies are
  // independent of one another.
  EXPECT_EQ(a.get(), 1);
  EXPECT_EQ(b.get(), 2);
}

TEST(ReflectionProtocolTest, ForwardingAfterMoveConstruction) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  protocol<Interface> a(Conforming{});
  a.set(5);
  protocol<Interface> b(std::move(a));

  EXPECT_EQ(b.get(), 5);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(a));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolTest, ForwardingAfterCopyAssignment) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  // Counter and Doubler both conform to Interface but have different
  // semantics for set(), so we can tell whether copy assignment updated
  // the vtable pointer.
  struct Counter {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  struct Doubler {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value * 2; }
  };

  protocol<Interface> a(Counter{});
  protocol<Interface> b(Doubler{});

  a = b;
  a.set(10);
  EXPECT_EQ(a.get(), 20);  // a now has Doubler's semantics.
}

TEST(ReflectionProtocolTest, ForwardingAfterMoveAssignment) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Counter {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  struct Doubler {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value * 2; }
  };

  protocol<Interface> a(Counter{});
  protocol<Interface> b(Doubler{});

  a = std::move(b);
  a.set(10);
  EXPECT_EQ(a.get(), 20);  // a now has Doubler's semantics.
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(b));
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolTest, ForwardingAfterSwap) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Counter {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  struct Doubler {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value * 2; }
  };

  protocol<Interface> a(Counter{});
  protocol<Interface> b(Doubler{});

  using std::swap;
  swap(a, b);

  a.set(10);
  EXPECT_EQ(a.get(), 20);  // a now behaves like Doubler.

  b.set(10);
  EXPECT_EQ(b.get(), 10);  // b now behaves like Counter.
}

TEST(ReflectionProtocolTest, ForwardingWithInPlaceConstruction) {
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int value;

    explicit Conforming(int initial_value) : value(initial_value) {}

    int get() const { return value; }
  };

  protocol<Interface> p(std::in_place_type<Conforming>, 42);
  EXPECT_EQ(p.get(), 42);
}

TEST(ReflectionProtocolTest, ForwardingWithCustomAllocator) {
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int value;

    explicit Conforming(int initial_value) : value(initial_value) {}

    int get() const { return value; }
  };

  unsigned allocs = 0;
  unsigned deallocs = 0;
  xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};

  protocol<Interface, xyz::TrackingAllocator<std::byte>> p(
      std::allocator_arg, alloc, Conforming(42));

  EXPECT_EQ(p.get(), 42);
  EXPECT_EQ(allocs, 1);
}

TEST(ReflectionProtocolTest, ConstMemberFunctionOnConstProtocol) {
  struct Interface {
    int get_value() const;
  };

  struct Conforming {
    int get_value() const { return 42; }
  };

  const protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.get_value(), 42);

  // A non-const member function is still not invocable on a const protocol;
  // that assertion is already covered above by
  // NonConstMemberFunctionNotInvocableFromConst.
}

TEST(ReflectionProtocolTest, OverloadsByParameterType) {
  struct Interface {
    int compute(int x);
    double compute(double x);
    std::string compute(const std::string& x) const;
  };

  struct Conforming {
    int compute(int x) { return x * 2; }

    double compute(double x) { return x * 3.0; }

    std::string compute(const std::string& x) const { return x + x; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.compute(5), 10);
  EXPECT_EQ(p.compute(5.0), 15.0);

  const auto& const_p = p;
  EXPECT_EQ(const_p.compute(std::string("A")), "AA");
}

TEST(ReflectionProtocolTest, OverloadsByArity) {
  struct Interface {
    int add(int a);
    int add(int a, int b);
  };

  struct Conforming {
    int add(int a) { return a; }

    int add(int a, int b) { return a + b; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.add(1), 1);
  EXPECT_EQ(p.add(1, 2), 3);
}

TEST(ReflectionProtocolTest, ConstAndNonConstOverloadPair) {
  struct Interface {
    int value() const;
    int value();
  };

  struct Conforming {
    int value() const { return 1; }

    int value() { return 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.value(), 2);

  const protocol<Interface>& const_p = p;
  EXPECT_EQ(const_p.value(), 1);
}

TEST(ReflectionProtocolTest, ConstProtocolExposesOnlyConstOverloads) {
  struct Interface {
    int get() const;
    void get(int value);
  };

  static_assert(!has_get_int<const protocol<Interface>>);
  static_assert(has_get_int<protocol<Interface>>);

  static_assert(has_get<protocol<Interface>>);
  static_assert(has_get<const protocol<Interface>>);
}

TEST(ReflectionProtocolTest, MemberThunksCannotBeDetachedForOverloads) {
  struct Interface {
    int compute(int x);
    double compute(double x);
    std::string compute(const std::string& x) const;
  };

  struct Conforming {
    int compute(int x) { return x * 2; }

    double compute(double x) { return x * 3.0; }

    std::string compute(const std::string& x) const { return x + x; }
  };

  protocol<Interface> p(Conforming{});

  static_assert(!std::is_copy_constructible_v<decltype(p.compute)>);
  static_assert(!std::is_move_constructible_v<decltype(p.compute)>);
  static_assert(!std::is_copy_assignable_v<decltype(p.compute)>);
  static_assert(!std::is_move_assignable_v<decltype(p.compute)>);
  static_assert(!std::is_default_constructible_v<decltype(p.compute)>);
  static_assert(!std::is_destructible_v<decltype(p.compute)>);
  static_assert(std::is_trivially_copyable_v<decltype(p.compute)>);
}

TEST(ReflectionProtocolTest, OverloadsThroughThunkReference) {
  struct Interface {
    int compute(int x);
    double compute(double x);
    std::string compute(const std::string& x) const;
  };

  struct Conforming {
    int compute(int x) { return x * 2; }

    double compute(double x) { return x * 3.0; }

    std::string compute(const std::string& x) const { return x + x; }
  };

  protocol<Interface> p(Conforming{});

  // Const propagates through `protocol`: the non-const overloads need a
  // non-const reference to the thunk.
  auto& compute = p.compute;
  EXPECT_EQ(compute(5), 10);
  EXPECT_EQ(compute(5.0), 15.0);
  const auto& const_compute = p.compute;
  EXPECT_EQ(const_compute(std::string("A")), "AA");
}

TEST(ReflectionProtocolTest, NoexceptOverload) {
  struct Interface {
    int f(int x) noexcept;
    int f(double x);
  };

  struct Conforming {
    int f(int x) noexcept { return x * 2; }

    int f(double x) { return static_cast<int>(x * 3.0); }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.f(5), 10);
  EXPECT_EQ(p.f(5.0), 15);
  static_assert(noexcept(p.f(1)));
  static_assert(!noexcept(p.f(1.0)));
}

// Tests that dispatching fails gracefully for a moved-from protocol, for
// named member functions. Operator dispatch has the same tests in
// protocol_operator_tests.cc.
#if (defined(_MSC_VER) && defined(_DEBUG)) || (!defined(NDEBUG))

TEST(ReflectionProtocolTest, MutableValuelessCall) {
  struct Interface {
    int foo();
  };

  struct TypeA {
    int foo() { return 5; }
  };

  protocol<Interface> p(TypeA{});
  EXPECT_EQ(p.foo(), 5);

  auto _ = std::move(p);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(p));

  EXPECT_DEATH(p.foo(), "cannot call member function of valueless protocol");
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolTest, ConstValuelessCall) {
  struct Interface {
    int foo() const;
  };

  struct TypeA {
    int foo() const { return 5; }
  };

  protocol<Interface> p(TypeA{});
  EXPECT_EQ(p.foo(), 5);

  auto _ = std::move(p);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(p));

  EXPECT_DEATH(p.foo(), "cannot call member function of valueless protocol");
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

#endif

TEST(ReflectionProtocolTest, VolatileConformingType) {
  struct Interface {
    int foo();
  };

  struct Conforming {
    int foo() volatile { return 10; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.foo(), 10);
}

TEST(ReflectionProtocolTest, RefQualifiers) {
  struct LvalueInterface {
    int foo() &;
  };

  struct RvalueInterface {
    int foo() &&;
  };

  struct Conforming {
    int foo() & { return 5; }

    int foo() && { return 10; }
  };

  protocol<LvalueInterface> p1(Conforming{});
  EXPECT_EQ(p1.foo(), 5);

  protocol<RvalueInterface> p2(Conforming{});
  EXPECT_EQ(std::move(p2).foo(), 10);
}

TEST(ReflectionProtocolTest, OverloadedQualifiers) {
  struct Interface {
    int foo() &;
    int foo() &&;
    int foo() const&;
  };

  struct Conforming {
    int foo() & { return 5; }

    int foo() && { return 10; }

    int foo() const& { return 20; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.foo(), 5);
  EXPECT_EQ(std::move(p).foo(), 10);

  const protocol<Interface> p2(Conforming{});
  EXPECT_EQ(p2.foo(), 20);
  // NOLINTBEGIN(hicpp-move-const-arg,performance-move-const-arg): The test
  // demonstrates dispatch through a const rvalue reference.
  EXPECT_EQ(std::move(p2).foo(), 20);
  // NOLINTEND(hicpp-move-const-arg,performance-move-const-arg)

  Conforming c{};
  protocol_view<Interface> p3(c);
  EXPECT_EQ(p3.foo(), 5);
  // NOLINTBEGIN(hicpp-move-const-arg,performance-move-const-arg): The test
  // demonstrates moving protocol_view on purpose.
  EXPECT_EQ(std::move(p3).foo(), 5);
  // NOLINTEND(hicpp-move-const-arg,performance-move-const-arg)

  protocol_view<const Interface> p4(c);
  EXPECT_EQ(p4.foo(), 20);
  // NOLINTBEGIN(hicpp-move-const-arg,performance-move-const-arg): The test
  // demonstrates moving protocol_view on purpose.
  EXPECT_EQ(std::move(p4).foo(), 20);
  // NOLINTEND(hicpp-move-const-arg,performance-move-const-arg)
}

TEST(ReflectionProtocolTest, ConstRefQualifiers) {
  struct Interface {
    int foo() &;
    int foo() &&;
    int foo() const&;
    int foo() const&&;
  };

  struct Conforming {
    int foo() & { return 1; }

    int foo() && { return 2; }

    int foo() const& { return 3; }

    int foo() const&& { return 4; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.foo(), 1);
  EXPECT_EQ(std::move(p).foo(), 2);

  const protocol<Interface> p2(Conforming{});
  EXPECT_EQ(p2.foo(), 3);
  // NOLINTBEGIN(hicpp-move-const-arg,performance-move-const-arg): The test
  // demonstrates dispatch through a const rvalue reference.
  EXPECT_EQ(std::move(p2).foo(), 4);
  // NOLINTEND(hicpp-move-const-arg,performance-move-const-arg)

  Conforming c{};
  protocol_view<Interface> p3(c);
  EXPECT_EQ(p3.foo(), 1);
  // NOLINTBEGIN(hicpp-move-const-arg,performance-move-const-arg): The test
  // demonstrates moving protocol_view on purpose.
  EXPECT_EQ(std::move(p3).foo(), 1);
  // NOLINTEND(hicpp-move-const-arg,performance-move-const-arg)

  protocol_view<const Interface> p4(c);
  EXPECT_EQ(p4.foo(), 3);
  // NOLINTBEGIN(hicpp-move-const-arg,performance-move-const-arg): The test
  // demonstrates moving protocol_view on purpose.
  EXPECT_EQ(std::move(p4).foo(), 3);
  // NOLINTEND(hicpp-move-const-arg,performance-move-const-arg)
}

TEST(ReflectionProtocolTest, RefQualifiersExplicitObject) {
  struct Interface {
    int foo() &;
    int foo() &&;
  };

  struct Conforming {
    int foo(this Conforming&) { return 5; }

    // NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved): The
    // parameter is unused.
    int foo(this Conforming&&) { return 10; }

    // NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved)
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.foo(), 5);
  EXPECT_EQ(std::move(p).foo(), 10);
}

}  // namespace

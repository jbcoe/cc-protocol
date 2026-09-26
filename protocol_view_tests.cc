// Tests for member function calls through protocol_view, and for views of
// protocols.

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "protocol.hh"
#include "test_helpers.h"
#include "tracking_allocator.h"

using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// Concepts for negative member function tests: a requires-expression naming a
// member that does not exist is only a substitution failure in a template.
template <typename P>
concept has_update = requires(P& p) { p.update(0); };

template <typename P>
concept has_get_value = requires(P& p) { p.get_value(); };

template <typename P>
concept has_get_int = requires(P& p) { p.get(0); };

template <typename P>
concept has_get = requires(P& p) { p.get(); };

// Member function signature tests for protocol_view.

TEST(ReflectionProtocolViewTest, ConstMemberFunction) {
  struct Interface {
    int get_value() const;
  };

  struct Conforming {
    int get_value() const { return 42; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.get_value(), 42);
}

TEST(ReflectionProtocolViewTest, NonConstMemberFunctionInvocableFromConstView) {
  struct Interface {
    void update(int value);
  };

  // `protocol_view` has shallow const: a const view still exposes the
  // non-const member functions of the interface.
  static_assert(requires(const protocol_view<Interface>& p) {
    { p.update(0) } -> std::same_as<void>;
  });
  static_assert(has_update<const protocol_view<Interface>>);
}

TEST(ReflectionProtocolViewTest, ConstViewExposesOnlyConstMemberFunctions) {
  struct Interface {
    int get_value() const;
    void update(int value);
  };

  static_assert(has_get_value<protocol_view<const Interface>>);
  static_assert(has_get_value<const protocol_view<const Interface>>);
  static_assert(!has_update<protocol_view<const Interface>>);
  static_assert(!has_update<const protocol_view<const Interface>>);

  static_assert(requires(const protocol_view<const Interface>& p) {
    { p.get_value() } -> std::same_as<int>;
  });
}

TEST(ReflectionProtocolViewTest, NonConstMemberFunctionCalledThroughConstView) {
  struct Interface {
    void update(int value);
  };

  struct Conforming {
    int last_value = 0;

    void update(int value) { last_value = value; }
  };

  Conforming c;
  const protocol_view<Interface> p(c);
  p.update(42);
  EXPECT_EQ(c.last_value, 42);
}

TEST(ReflectionProtocolViewTest, ConstViewCallsConstMemberFunction) {
  struct Interface {
    int get_value() const;
    void update(int value);
  };

  struct Conforming {
    int value = 7;

    int get_value() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  const Conforming const_object;
  protocol_view<const Interface> view_of_const(const_object);
  EXPECT_EQ(view_of_const.get_value(), 7);

  Conforming mutable_object;
  mutable_object.update(9);
  const protocol_view<const Interface> view_of_mutable(mutable_object);
  EXPECT_EQ(view_of_mutable.get_value(), 9);
}

TEST(ReflectionProtocolViewTest, ConstViewOfInterfaceWithNoConstMembers) {
  struct Interface {
    void update(int value);
  };

  // A const view of an interface with no const member functions is
  // well-formed but exposes nothing.
  static_assert(!has_update<protocol_view<const Interface>>);
  static_assert(std::is_copy_constructible_v<protocol_view<const Interface>>);
}

TEST(ReflectionProtocolViewTest, SingleParameterMemberFunction) {
  struct Interface {
    void update(int value);
  };

  struct Conforming {
    int last_value = 0;

    void update(int value) { last_value = value; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  p.update(42);
  EXPECT_EQ(c.last_value, 42);
  static_assert(!noexcept(c.update(1.0)));
}

TEST(ReflectionProtocolViewTest, NoexceptMemberFunction) {
  struct Interface {
    double compute(double input) noexcept;
  };

  struct Conforming {
    double compute(double input) noexcept { return input * 2.0; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.compute(21.0), 42.0);
  static_assert(noexcept(p.compute(1.0)));
}

TEST(ReflectionProtocolViewTest, MultiParameterMemberFunction) {
  struct Interface {
    int add(int a, int b) const;
  };

  struct Conforming {
    int add(int a, int b) const { return a + b; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.add(1, 2), 3);
}

TEST(ReflectionProtocolViewTest, VoidMemberFunction) {
  struct Interface {
    void reset();
  };

  struct Conforming {
    bool was_reset = false;

    void reset() { was_reset = true; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  p.reset();
  EXPECT_TRUE(c.was_reset);
}

TEST(ReflectionProtocolViewTest, MultipleMemberFunctions) {
  struct Interface {
    double add(double x, double y) const noexcept;
    double multiply(double x, double y) const noexcept;
  };

  struct Conforming {
    double add(double x, double y) const noexcept { return x + y; }

    double multiply(double x, double y) const noexcept { return x * y; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.add(1.0, 2.0), 3.0);
  EXPECT_EQ(p.multiply(3.0, 4.0), 12.0);
}

TEST(ReflectionProtocolViewTest, MixedConstAndMutatingMemberFunctions) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.get(), 0);
  p.set(7);
  EXPECT_EQ(p.get(), 7);
  EXPECT_EQ(c.value, 7);
}

TEST(ReflectionProtocolViewTest, OverloadsByParameterType) {
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

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.compute(5), 10);
  EXPECT_EQ(p.compute(5.0), 15.0);
  EXPECT_EQ(p.compute(std::string("A")), "AA");
}

TEST(ReflectionProtocolViewTest,
     ConstAndNonConstOverloadPairDispatchesToNonConst) {
  struct Interface {
    int value() const;
    int value();
  };

  struct Conforming {
    int value() const { return 1; }

    int value() { return 2; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view.value(), 2);

  // Shallow const: a const protocol_view still dispatches to the non-const
  // overload.
  const protocol_view<Interface>& const_view = view;
  EXPECT_EQ(const_view.value(), 2);
}

TEST(ReflectionProtocolViewTest, ConstViewDispatchesToConstOverload) {
  struct Interface {
    int value() const;
    int value();
  };

  struct Conforming {
    int value() const { return 1; }

    int value() { return 2; }
  };

  Conforming c;
  protocol_view<const Interface> view(c);
  EXPECT_EQ(view.value(), 1);

  const Conforming const_c;
  protocol_view<const Interface> const_view(const_c);
  EXPECT_EQ(const_view.value(), 1);
}

TEST(ReflectionProtocolViewTest, ConstViewExposesOnlyConstOverloads) {
  struct Interface {
    int get() const;
    void get(int value);
  };

  static_assert(!has_get_int<protocol_view<const Interface>>);
  static_assert(has_get_int<protocol_view<Interface>>);
  static_assert(has_get_int<const protocol_view<Interface>>);

  static_assert(has_get<protocol_view<const Interface>>);
  static_assert(has_get<protocol_view<Interface>>);
  static_assert(has_get<const protocol_view<Interface>>);
}

TEST(ReflectionProtocolViewTest, IsTriviallyCopyableWithOverloads) {
  struct Interface {
    int compute(int x);
    double compute(double x);
    std::string compute(const std::string& x) const;
  };

  static_assert(std::is_trivially_copyable_v<protocol_view<Interface>>);
  static_assert(std::is_trivially_copyable_v<protocol_view<const Interface>>);
  static_assert(sizeof(protocol_view<Interface>) == 2 * sizeof(void*));
}

TEST(ReflectionProtocolViewTest, MemberThunksCannotBeDetachedForOverloads) {
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

  Conforming c;
  protocol_view<Interface> p(c);

  static_assert(!std::is_copy_constructible_v<decltype(p.compute)>);
  static_assert(!std::is_move_constructible_v<decltype(p.compute)>);
  static_assert(!std::is_copy_assignable_v<decltype(p.compute)>);
  static_assert(!std::is_move_assignable_v<decltype(p.compute)>);
  static_assert(!std::is_default_constructible_v<decltype(p.compute)>);
  static_assert(!std::is_destructible_v<decltype(p.compute)>);
  static_assert(std::is_trivially_copyable_v<decltype(p.compute)>);
}

TEST(ReflectionProtocolViewTest, OverloadsThroughThunkReference) {
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

  Conforming c;
  protocol_view<Interface> p(c);

  // `protocol_view` is shallow const: the non-const overloads are callable
  // through a const reference to the thunk.
  const auto& compute = p.compute;
  EXPECT_EQ(compute(5), 10);
  EXPECT_EQ(compute(5.0), 15.0);
  EXPECT_EQ(compute(std::string("A")), "AA");
}

// Call operator tests for protocol_view.

TEST(ReflectionProtocolViewTest, CallOperator) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view(21), 42);
}

TEST(ReflectionProtocolViewTest, CallOperatorOverloadSetCannotBeDetached) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }
  };

  Conforming c;
  protocol_view<Interface> view(c);

  // Slicing `view` into its `operator_overload_set` base would detach the
  // rest of `view`'s layout, so the base's own `static_cast<ProtocolType*>`
  // back to the full object inside `operator()` would be undefined
  // behaviour.
  static_assert(!operator_overload_set_can_be_sliced_from<decltype(view)>);
}

TEST(ReflectionProtocolViewTest, CallOperatorFromLambda) {
  struct Interface {
    int operator()(int x) const;
  };

  // protocol_view can be constructed from a lambda directly, like
  // function_ref.
  auto lambda = [](int x) { return x * 2; };
  protocol_view<Interface> view(lambda);
  EXPECT_EQ(view(21), 42);
}

TEST(ReflectionProtocolViewTest, OverloadedCallOperators) {
  struct Interface {
    int operator()(int x);
    double operator()(double x);
    std::string operator()(const std::string& x) const;
  };

  struct Conforming {
    int operator()(int x) { return x * 2; }

    double operator()(double x) { return x * 3.0; }

    std::string operator()(const std::string& x) const { return x + x; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view(5), 10);
  EXPECT_EQ(view(5.0), 15.0);
  EXPECT_EQ(view(std::string("A")), "AA");
}

TEST(ReflectionProtocolViewTest,
     ConstAndNonConstCallOperatorPairDispatchesToNonConst) {
  struct Interface {
    int operator()() const;
    int operator()();
  };

  struct Conforming {
    int operator()() const { return 1; }

    int operator()() { return 2; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view(), 2);

  // Shallow const: a const protocol_view still dispatches to the non-const
  // overload.
  const protocol_view<Interface>& const_view = view;
  EXPECT_EQ(const_view(), 2);
}

TEST(ReflectionProtocolViewTest, ConstViewDispatchesToConstCallOperator) {
  struct Interface {
    int operator()() const;
    int operator()();
  };

  struct Conforming {
    int operator()() const { return 1; }

    int operator()() { return 2; }
  };

  Conforming c;
  protocol_view<const Interface> view(c);
  EXPECT_EQ(view(), 1);

  const Conforming const_c;
  protocol_view<const Interface> const_view(const_c);
  EXPECT_EQ(const_view(), 1);
}

TEST(ReflectionProtocolViewTest, ConstViewExposesOnlyConstCallOperators) {
  struct Interface {
    int operator()() const;
    void operator()(int value);
  };

  static_assert(!is_callable_with_int<protocol_view<const Interface>>);
  static_assert(is_callable_with_int<protocol_view<Interface>>);
  static_assert(is_callable_with_int<const protocol_view<Interface>>);

  static_assert(is_callable<protocol_view<const Interface>>);
  static_assert(is_callable<protocol_view<Interface>>);
  static_assert(is_callable<const protocol_view<Interface>>);
}

TEST(ReflectionProtocolViewTest, CallOperatorAlongsideNamedMembers) {
  struct Interface {
    int operator()(int x) const;
    int get() const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }

    int get() const { return 7; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view(2), 4);
  EXPECT_EQ(view.get(), 7);
}

TEST(ReflectionProtocolViewTest, IsTriviallyCopyableWithCallOperator) {
  struct Interface {
    int operator()(int x);
    double operator()(double x);
    std::string operator()(const std::string& x) const;
  };

  static_assert(std::is_trivially_copyable_v<protocol_view<Interface>>);
  static_assert(std::is_trivially_copyable_v<protocol_view<const Interface>>);
  static_assert(sizeof(protocol_view<Interface>) == 2 * sizeof(void*));
}

TEST(ReflectionProtocolViewTest, StaticMemberFunction) {
  struct Interface {
    int value() const;
  };

  struct Conforming {
    static int value() { return 42; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view.value(), 42);
}

TEST(ReflectionProtocolViewTest, ConstViewCallsStaticMemberFunction) {
  struct Interface {
    int value() const;
  };

  struct Conforming {
    static int value() { return 42; }
  };

  const Conforming c;
  protocol_view<const Interface> view(c);
  EXPECT_EQ(view.value(), 42);
}

TEST(ReflectionProtocolViewTest, StaticMemberFunctionSatisfiesNonConstMember) {
  struct Interface {
    int next();
  };

  struct Conforming {
    static int next() {
      static int counter = 0;
      return ++counter;
    }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view.next(), 1);
  EXPECT_EQ(view.next(), 2);
}

TEST(ReflectionProtocolViewTest,
     StaticMemberFunctionAlongsideNonStaticMembers) {
  struct Interface {
    int value() const;
    int add(int x);
  };

  struct Conforming {
    int total = 0;

    static int value() { return 3; }

    int add(int x) {
      total += x;
      return total;
    }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view.value(), 3);
  EXPECT_EQ(view.add(4), 4);
  EXPECT_EQ(view.add(5), 9);
}

TEST(ReflectionProtocolViewTest, StaticCallOperator) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    static int operator()(int x) { return x * 3; }
  };

  Conforming c;
  protocol_view<Interface> view(c);
  EXPECT_EQ(view(5), 15);
}

// ---------------------------------------------------------------------------
// Views of protocols: a `protocol_view<I>` or `protocol_view<const I>` views
// the object a `protocol<I>` owns, sharing its vtable; a
// `protocol_view<const I>` can also be constructed from a
// `protocol_view<I>`.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolViewTest, IsConstructibleFromProtocol) {
  struct Interface {
    int get() const;
  };

  static_assert(
      std::is_constructible_v<protocol_view<Interface>, protocol<Interface>&>);
  static_assert(
      std::is_convertible_v<protocol<Interface>&, protocol_view<Interface>>);
  static_assert(!std::is_constructible_v<protocol_view<Interface>,
                                         const protocol<Interface>&>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, protocol<Interface>>);
}

TEST(ReflectionProtocolViewTest, ConstViewIsConstructibleFromProtocol) {
  struct Interface {
    int get() const;
  };

  static_assert(std::is_constructible_v<protocol_view<const Interface>,
                                        const protocol<Interface>&>);
  static_assert(std::is_constructible_v<protocol_view<const Interface>,
                                        protocol<Interface>&>);
  static_assert(std::is_convertible_v<const protocol<Interface>&,
                                      protocol_view<const Interface>>);
  static_assert(std::is_convertible_v<protocol<Interface>&,
                                      protocol_view<const Interface>>);
  static_assert(!std::is_constructible_v<protocol_view<const Interface>,
                                         protocol<Interface>>);
  static_assert(!std::is_constructible_v<protocol_view<const Interface>,
                                         const protocol<Interface>>);
}

TEST(ReflectionProtocolViewTest, ViewOfProtocolCallsOwnedObject) {
  struct Interface {
    int get() const;
    void update(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  protocol<Interface> p(Conforming{});
  protocol_view<Interface> view(p);

  view.update(7);
  EXPECT_EQ(p.get(), 7);

  p.update(11);
  EXPECT_EQ(view.get(), 11);
}

TEST(ReflectionProtocolViewTest, ConstViewOfProtocolExposesOnlyConstMembers) {
  struct Interface {
    int get() const;
    void update(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  const protocol<Interface> p(Conforming{3});
  protocol_view<const Interface> view(p);

  EXPECT_EQ(view.get(), 3);
  static_assert(!has_update<protocol_view<const Interface>>);
}

TEST(ReflectionProtocolViewTest, ViewOfProtocolWithOverloadsAndCallOperator) {
  struct Interface {
    int get() const;
    int get(int value) const;
    int operator()(int value) const;
  };

  struct Conforming {
    int get() const { return 1; }

    int get(int value) const { return value + 1; }

    int operator()(int value) const { return value * 2; }
  };

  protocol<Interface> p(Conforming{});
  protocol_view<Interface> view(p);

  EXPECT_EQ(view.get(), 1);
  EXPECT_EQ(view.get(4), 5);
  EXPECT_EQ(view(21), 42);
}

TEST(ReflectionProtocolViewTest, ViewOfProtocolRemainsValidAfterMove) {
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int get() const { return 5; }
  };

  protocol<Interface> p(Conforming{});
  protocol_view<Interface> view(p);

  protocol<Interface> moved_to(std::move(p));

  EXPECT_EQ(view.get(), 5);
}

TEST(ReflectionProtocolViewTest, ViewOfProtocolWithCustomAllocator) {
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int get() const { return 9; }
  };

  unsigned allocs = 0;
  unsigned deallocs = 0;
  xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};

  protocol<Interface, xyz::TrackingAllocator<std::byte>> p(std::allocator_arg,
                                                           alloc, Conforming{});
  protocol_view<Interface> view(p);

  EXPECT_EQ(view.get(), 9);
}

TEST(ReflectionProtocolViewTest, ConstViewIsConstructibleFromView) {
  struct Interface {
    int get() const;
    void update(int value);
  };

  static_assert(std::is_convertible_v<protocol_view<Interface>,
                                      protocol_view<const Interface>>);
  static_assert(!std::is_constructible_v<protocol_view<Interface>,
                                         protocol_view<const Interface>>);
}

TEST(ReflectionProtocolViewTest, ConstViewOfViewCallsViewedObject) {
  struct Interface {
    int get() const;
    void update(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  protocol<Interface> p(Conforming{});
  protocol_view<Interface> view(p);
  protocol_view<const Interface> const_view(view);

  view.update(7);
  EXPECT_EQ(const_view.get(), 7);
  static_assert(!has_update<protocol_view<const Interface>>);
}

TEST(ReflectionProtocolViewTest, ViewOfProtocolPassedByValue) {
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int value;

    int get() const { return value; }
  };

  auto read = [](protocol_view<const Interface> view) { return view.get(); };

  protocol<Interface> p(Conforming{5});
  EXPECT_EQ(read(p), 5);
}

TEST(ReflectionProtocolViewTest, MutableViewOfProtocolPassedByValue) {
  struct Interface {
    int get() const;
    void update(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  auto write = [](protocol_view<Interface> view) { view.update(9); };

  protocol<Interface> p(Conforming{});
  write(p);
  EXPECT_EQ(p.get(), 9);
}

TEST(ReflectionProtocolViewTest, ViewOfConformingObjectPassedByValue) {
  struct Interface {
    int get() const;
    void update(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  auto read = [](protocol_view<const Interface> view) { return view.get(); };
  auto write = [](protocol_view<Interface> view) { view.update(4); };

  Conforming c{};
  write(c);
  EXPECT_EQ(read(c), 4);
}

TEST(ReflectionProtocolViewTest, LvalueRefQualifier) {
  struct LvalueInterface {
    int foo() &;
  };

  struct Conforming {
    int foo() & { return 5; }
  };

  Conforming c{};
  protocol_view<LvalueInterface> p(c);
  EXPECT_EQ(p.foo(), 5);
}

}  // namespace

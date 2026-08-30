// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.

#include "protocol.hh"

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "tracking_allocator.h"

using xyz::reflection::is_protocol_conformant;
using xyz::reflection::is_protocol_v;
using xyz::reflection::is_protocol_view_v;
using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// Concepts for negative member function tests: a requires-expression naming a
// member that does not exist is only a substitution failure in a template.
template <typename P>
concept has_update = requires(P& p) { p.update(0); };

template <typename P>
concept has_get_value = requires(P& p) { p.get_value(); };

// Concepts for overloaded `get`/`get(int)` negative tests.
template <typename P>
concept has_get_int = requires(P& p) { p.get(0); };

template <typename P>
concept has_get = requires(P& p) { p.get(); };

// Concepts for call operator tests.
template <typename P>
concept is_callable = requires(P& p) { p(); };

template <typename P>
concept is_callable_with_int = requires(P& p) { p(0); };

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
  // `protocol` is not default-constructible and cannot be copied, or
  // assigned to if the interface type cannot be copied.
  // `protocol` can be unconditionally move constructed and move assigned.
  struct D {
    D() = delete;
    D(const D&) = delete;
    D(D&&) = delete;
    D& operator=(const D&) = delete;
    D& operator=(D&&) = delete;
  };

  static_assert(!std::is_default_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_constructible_v<protocol<D>>);
  static_assert(std::is_move_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_assignable_v<protocol<D>>);
  static_assert(std::is_move_assignable_v<protocol<D>>);
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

  protocol_view<A> move_constructed(std::move(copy_constructed));
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
  yet_another_view = std::move(other_view);
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
  EXPECT_TRUE(copy_constructed.valueless_after_move());
  EXPECT_EQ(move_constructed.get(), 2);

  protocol<A> copy_assigned(Conforming{});
  copy_assigned = original;
  copy_assigned.set(3);
  EXPECT_EQ(original.get(), 1);
  EXPECT_EQ(copy_assigned.get(), 3);

  protocol<A> move_assigned(Conforming{});
  move_assigned = std::move(copy_assigned);
  EXPECT_EQ(move_assigned.get(), 3);
  EXPECT_TRUE(copy_assigned.valueless_after_move());
}

// ---------------------------------------------------------------------------
// Conformance check tests.
// ---------------------------------------------------------------------------

TEST(ConformsToTest, EmptyInterfaceIsAlwaysSatisfied) {
  struct EmptyInterface {};

  struct Candidate {};

  static_assert(is_protocol_conformant<EmptyInterface, Candidate>());
}

TEST(ConformsToTest, CandidateTypeConformsWhenAllMethodsMatch) {
  struct Interface {
    std::string_view name() const noexcept;
    int count();
  };

  struct Candidate {
    std::string_view name() const noexcept;
    int count();
  };

  static_assert(is_protocol_conformant<Interface, Candidate>());
}

TEST(ConformsToTest, CandidateTypeConformsWithExtraMethodsPresent) {
  struct Interface {
    void process();
  };

  struct CandidateWithExtra {
    void process();
    void extra_method();
    int another() const;
  };

  static_assert(is_protocol_conformant<Interface, CandidateWithExtra>());
}

TEST(ConformsToTest, CandidateTypeMissingMethodDoesNotConform) {
  struct Interface {
    void foo();
    void bar();
  };

  struct MissingBar {
    void foo();
  };

  static_assert(!is_protocol_conformant<Interface, MissingBar>());
}

TEST(ConformsToTest, WrongConstnessDoesNotConform) {
  struct Interface {
    int value() const;
  };

  struct NonConst {
    int value();  // not const — does not match the interface
  };

  static_assert(!is_protocol_conformant<Interface, NonConst>());
}

TEST(ConformsToTest, WrongReturnTypeDoesNotConform) {
  struct Interface {
    int compute();
  };

  struct WrongReturn {
    double compute();
  };

  static_assert(!is_protocol_conformant<Interface, WrongReturn>());
}

TEST(ConformsToTest, WrongParameterTypeDoesNotConform) {
  struct Interface {
    void process(int value);
  };

  struct WrongParam {
    void process(double value);
  };

  static_assert(!is_protocol_conformant<Interface, WrongParam>());
}

TEST(ConformsToTest, WrongParameterCountDoesNotConform) {
  struct Interface {
    void process(int a, int b);
  };

  struct WrongArity {
    void process(int a);
  };

  static_assert(!is_protocol_conformant<Interface, WrongArity>());
}

TEST(ConformsToTest, MultipleParametersMatchCorrectly) {
  struct Interface {
    void write(int length, double value);
  };

  struct Candidate {
    void write(int length, double value);
  };

  static_assert(is_protocol_conformant<Interface, Candidate>());
}

TEST(ConformsToTest, CandidateTypeConformsForTypicalInterfaceB) {
  struct InterfaceB {
    void process(const std::string& input);
    std::vector<int> get_results() const;
    bool is_ready() const;
  };

  struct CandidateB {
    void process(const std::string& input);
    std::vector<int> get_results() const;
    bool is_ready() const;
  };

  static_assert(is_protocol_conformant<InterfaceB, CandidateB>());
}

TEST(ConformsToTest, NoexceptInterfaceRequiresNoexceptCandidate) {
  struct Interface {
    void f() noexcept;
  };

  struct Conforming {
    void f() noexcept;
  };

  struct NonNoexcept {
    void f();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, NonNoexcept>());
}

TEST(ConformsToTest, NonNoexceptInterfaceAcceptsNoexceptCandidate) {
  struct Interface {
    void f();
  };

  struct NoexceptCandidate {
    void f() noexcept;
  };

  static_assert(is_protocol_conformant<Interface, NoexceptCandidate>());
}

TEST(ConformsToTest, LvalueRefQualifierMustMatch) {
  struct Interface {
    void f() &;
  };

  struct Conforming {
    void f() &;
  };

  struct UnqualifiedCandidate {
    void f();
  };

  struct RvalueRefCandidate {
    void f() &&;
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, UnqualifiedCandidate>());
  static_assert(!is_protocol_conformant<Interface, RvalueRefCandidate>());
}

TEST(ConformsToTest, RvalueRefQualifierMustMatch) {
  struct Interface {
    void f() &&;
  };

  struct Conforming {
    void f() &&;
  };

  struct UnqualifiedCandidate {
    void f();
  };

  struct LvalueRefCandidate {
    void f() &;
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, UnqualifiedCandidate>());
  static_assert(!is_protocol_conformant<Interface, LvalueRefCandidate>());
}

TEST(ConformsToTest, UnqualifiedInterfaceDoesNotMatchRefQualifiedCandidate) {
  struct Interface {
    void f();
  };

  struct LvalueRefCandidate {
    void f() &;
  };

  struct RvalueRefCandidate {
    void f() &&;
  };

  static_assert(!is_protocol_conformant<Interface, LvalueRefCandidate>());
  static_assert(!is_protocol_conformant<Interface, RvalueRefCandidate>());
}

TEST(ConformsToTest, OverloadedMemberFunctionsConform) {
  struct Interface {
    int compute(int value);
    double compute(double value);
    std::string compute(const std::string& value) const;
  };

  struct Conforming {
    int compute(int value) { return value; }

    double compute(double value) { return value; }

    std::string compute(const std::string& value) const { return value; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, CandidateMissingOverloadDoesNotConform) {
  struct Interface {
    int compute(int value);
    double compute(double value);
    std::string compute(const std::string& value) const;
  };

  struct MissingOverloads {
    int compute(int value) { return value; }
  };

  static_assert(!is_protocol_conformant<Interface, MissingOverloads>());
}

TEST(ConformsToTest, CandidateWithExtraOverloadsConforms) {
  struct Interface {
    int compute(int value);
  };

  struct CandidateWithExtraOverload {
    int compute(int value) { return value; }

    double compute(double value) { return value; }
  };

  static_assert(
      is_protocol_conformant<Interface, CandidateWithExtraOverload>());
}

TEST(ConformsToTest, OverloadsByArityConform) {
  struct Interface {
    int f(int a);
    int f(int a, int b);
  };

  struct Conforming {
    int f(int a) { return a; }

    int f(int a, int b) { return a + b; }
  };

  struct MissingUnaryOverload {
    int f(int a, int b) { return a + b; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, MissingUnaryOverload>());
}

TEST(ConformsToTest, ConstAndNonConstOverloadPairConforms) {
  struct Interface {
    int f() const;
    int f();
  };

  struct Conforming {
    int f() const { return 1; }

    int f() { return 2; }
  };

  struct ConstOnly {
    int f() const { return 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, ConstOnly>());
}

TEST(ConformsToTest, CallOperatorConforms) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x + 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());

  auto lambda = [](int x) { return x + 1; };
  static_assert(is_protocol_conformant<Interface, decltype(lambda)>());
}

TEST(ConformsToTest, CallOperatorWithWrongSignatureDoesNotConform) {
  struct Interface {
    int operator()(int x) const;
  };

  struct WrongParam {
    int operator()(double x) const { return static_cast<int>(x); }
  };

  struct NonConst {
    int operator()(int x) { return x; }
  };

  static_assert(!is_protocol_conformant<Interface, WrongParam>());
  static_assert(!is_protocol_conformant<Interface, NonConst>());
}

TEST(ConformsToTest, MutableLambdaConformsToNonConstCallOperator) {
  struct Interface {
    int operator()(int x);
  };

  auto mutable_lambda = [](int x) mutable { return x + 1; };
  auto const_lambda = [](int x) { return x + 1; };

  static_assert(is_protocol_conformant<Interface, decltype(mutable_lambda)>());
  static_assert(!is_protocol_conformant<Interface, decltype(const_lambda)>());
}

TEST(ConformsToTest, OverloadedCallOperatorsConform) {
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

  struct MissingOverloads {
    int operator()(int x) { return x * 2; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, MissingOverloads>());
}

TEST(ConformsToTest, StaticCandidateConformsToConstMember) {
  struct Interface {
    int value() const;
  };

  struct Conforming {
    static int value();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticCandidateConformsToNonConstMember) {
  struct Interface {
    int next();
  };

  struct Conforming {
    static int next();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticCandidateConformsToRefQualifiedMember) {
  struct Interface {
    int get() &;
    int take() &&;
  };

  struct Conforming {
    static int get();

    static int take();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticCandidateWithWrongSignatureDoesNotConform) {
  struct Interface {
    int value(int x) const;
  };

  struct WrongParam {
    static int value(double x);
  };

  struct WrongReturn {
    static double value(int x);
  };

  static_assert(!is_protocol_conformant<Interface, WrongParam>());
  static_assert(!is_protocol_conformant<Interface, WrongReturn>());
}

TEST(ConformsToTest, NoexceptInterfaceRequiresNoexceptStaticCandidate) {
  struct Interface {
    int value() const noexcept;
  };

  struct Conforming {
    static int value() noexcept;
  };

  struct NonNoexcept {
    static int value();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, NonNoexcept>());
}

TEST(ConformsToTest, InterfaceStaticMembersAreIgnored) {
  struct Interface {
    static int helper();
    int value() const;
  };

  struct Conforming {
    int value() const { return 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticCallOperatorConforms) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    static int operator()(int x) { return x + 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticOverloadConformsAlongsideNonStatic) {
  struct Interface {
    int f(int x) const;
    int f(double x) const;
  };

  struct Conforming {
    int f(int x) const { return x; }

    static int f(double x) { return static_cast<int>(x); }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
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
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, NonConforming&>);
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
  static_assert(!std::is_constructible_v<protocol_view<const Interface>,
                                         const NonConforming&>);
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
  protocol<Interface> b(a);
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
  EXPECT_TRUE(a.valueless_after_move());
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
  EXPECT_TRUE(b.valueless_after_move());
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

// Call operator tests for protocol.

TEST(ReflectionProtocolTest, CallOperator) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p(21), 42);
}

TEST(ReflectionProtocolTest, CallOperatorFromLambda) {
  struct Interface {
    int operator()(int x) const;
  };

  protocol<Interface> p([](int x) { return x * 2; });
  EXPECT_EQ(p(21), 42);
}

TEST(ReflectionProtocolTest, OverloadedCallOperators) {
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

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p(5), 10);
  EXPECT_EQ(p(5.0), 15.0);

  const auto& const_p = p;
  EXPECT_EQ(const_p(std::string("A")), "AA");
}

TEST(ReflectionProtocolTest, ConstAndNonConstCallOperatorPair) {
  struct Interface {
    int operator()() const;
    int operator()();
  };

  struct Conforming {
    int operator()() const { return 1; }

    int operator()() { return 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p(), 2);

  const protocol<Interface>& const_p = p;
  EXPECT_EQ(const_p(), 1);
}

TEST(ReflectionProtocolTest, ConstProtocolExposesOnlyConstCallOperators) {
  struct Interface {
    int operator()() const;
    void operator()(int value);
  };

  static_assert(!is_callable_with_int<const protocol<Interface>>);
  static_assert(is_callable_with_int<protocol<Interface>>);

  static_assert(is_callable<protocol<Interface>>);
  static_assert(is_callable<const protocol<Interface>>);
}

TEST(ReflectionProtocolTest, CallOperatorAlongsideNamedMembers) {
  struct Interface {
    int operator()(int x) const;
    int get() const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }

    int get() const { return 7; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p(2), 4);
  EXPECT_EQ(p.get(), 7);
}

TEST(ReflectionProtocolTest, NoexceptCallOperator) {
  struct Interface {
    int operator()(int x) noexcept;
  };

  struct Conforming {
    int operator()(int x) noexcept { return x * 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p(21), 42);
  static_assert(noexcept(p(1)));
}

TEST(ReflectionProtocolTest, CallOperatorForwardingAfterCopy) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }
  };

  protocol<Interface> p(Conforming{});
  protocol<Interface> copy(p);
  EXPECT_EQ(copy(21), 42);
}
}  // namespace

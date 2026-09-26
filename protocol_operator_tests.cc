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
// Tests for protocol/protocol_view's synthesised operators: the call
// operator and each operator generate_vtable_specs forwards.

#include <gtest/gtest.h>

#include <compare>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "protocol.hh"
#include "test_helpers.h"

using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

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

TEST(ReflectionProtocolTest, CallOperatorOverloadSetCannotBeDetached) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x * 2; }
  };

  protocol<Interface> p(Conforming{});

  // Slicing `p` into its `operator_overload_set` base would detach the
  // rest of `p`'s layout, so the base's own `static_cast<ProtocolType*>`
  // back to the full object inside `operator()` would be undefined
  // behaviour.
  static_assert(!operator_overload_set_can_be_sliced_from<decltype(p)>);
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
  // NOLINTBEGIN(performance-unnecessary-copy-initialization): the test
  // exercises copy construction on purpose.
  protocol<Interface> copy(p);
  // NOLINTEND(performance-unnecessary-copy-initialization)
  EXPECT_EQ(copy(21), 42);
}

// Tests that dispatching through a moved-from protocol fails gracefully.
// Exercised only for the call operator: every operator thunk dispatches
// through the same shared valueless check, so one death test covers the
// guard for all of them.
#if (defined(_MSC_VER) && defined(_DEBUG)) || (!defined(NDEBUG))

TEST(ReflectionProtocolTest, MutableCallOperatorValuelessCall) {
  struct Interface {
    int operator()(int x);
  };

  struct TypeA {
    int operator()(int x) { return x * 2; }
  };

  protocol<Interface> p(TypeA{});
  EXPECT_EQ(p(21), 42);

  auto _ = std::move(p);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(p));

  EXPECT_DEATH(p(21), "cannot call member function of valueless protocol");
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

TEST(ReflectionProtocolTest, ConstCallOperatorValuelessCall) {
  struct Interface {
    int operator()(int x) const;
  };

  struct TypeA {
    int operator()(int x) const { return x * 2; }
  };

  protocol<Interface> p(TypeA{});
  EXPECT_EQ(p(21), 42);

  auto _ = std::move(p);
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // test exercises the moved-from state on purpose.
  EXPECT_TRUE(valueless_after_move(p));

  EXPECT_DEATH(p(21), "cannot call member function of valueless protocol");
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

#endif

TEST(ReflectionProtocolTest, OperatorSquareBrackets) {
  struct Interface {
    int operator[](int) const noexcept;
    int operator[](int) noexcept;
  };

  struct Conforming {
    int operator[](int) const noexcept { return 0; }

    int operator[](int) noexcept { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p[0], 42);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv[0], 42);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv[0], 0);
}

TEST(ReflectionProtocolTest, OperatorStar) {
  struct Interface {
    int operator*() const noexcept;
    int operator*() noexcept;
  };

  struct Conforming {
    int operator*() const noexcept { return 0; }

    int operator*() noexcept { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(*p, 42);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(*pv, 42);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(*pcv, 0);
}

TEST(ReflectionProtocolViewTest,
     OverloadsDifferingOnlyByReturnTypeAreNotAmbiguous) {
  struct Interface {
    const int& operator*() const noexcept;
    int& operator*() noexcept;
  };

  struct Conforming {
    int value = 0;

    const int& operator*() const noexcept { return value; }

    int& operator*() noexcept { return value; }
  };

  Conforming c;

  protocol_view<Interface> pv(c);
  EXPECT_EQ(*pv, 0);
  *pv = 5;
  EXPECT_EQ(*pv, 5);
}

TEST(ReflectionProtocolTest, OperatorStarBinary) {
  struct Interface {
    int operator*(int rhs) const noexcept;
    int operator*(int rhs) noexcept;
  };

  struct Conforming {
    int operator*(int rhs) const noexcept { return rhs; }

    int operator*(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p * 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv * 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv * 3, 3);
}

TEST(ReflectionProtocolTest, OperatorArrow) {
  struct Interface {
    int operator->() const noexcept;
    int operator->() noexcept;
  };

  struct Conforming {
    int operator->() const noexcept { return 0; }

    int operator->() noexcept { return 42; }
  };

  Conforming c;

  protocol<Interface> p(c);
  EXPECT_EQ(p.operator->(), 42);

  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv.operator->(), 42);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv.operator->(), 0);
}

TEST(ReflectionProtocolTest, OperatorArrowStar) {
  struct Interface {
    int operator->*(int rhs) const noexcept;
    int operator->*(int rhs) noexcept;
  };

  struct Conforming {
    int operator->*(int rhs) const noexcept { return rhs; }

    int operator->*(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p->*3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv->*3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv->*3, 3);
}

TEST(ReflectionProtocolTest, OperatorTilde) {
  struct Interface {
    int operator~() const noexcept;
    int operator~() noexcept;
  };

  struct Conforming {
    int operator~() const noexcept { return 0; }

    int operator~() noexcept { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(~p, 42);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(~pv, 42);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(~pcv, 0);
}

TEST(ReflectionProtocolTest, OperatorLogicalNot) {
  struct Interface {
    bool operator!() const noexcept;
    bool operator!() noexcept;
  };

  struct Conforming {
    bool operator!() const noexcept { return false; }

    bool operator!() noexcept { return true; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(!p);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(!pv);

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(!pcv);
}

TEST(ReflectionProtocolTest, ConversionToBool) {
  struct Interface {
    explicit operator bool() const noexcept;
    explicit operator bool() noexcept;
  };

  struct Conforming {
    explicit operator bool() const noexcept { return false; }

    explicit operator bool() noexcept { return true; }
  };

  static_assert(std::is_constructible_v<bool, protocol<Interface>>);
  static_assert(!std::is_convertible_v<protocol<Interface>, bool>);

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(static_cast<bool>(p));

  const protocol<Interface> const_p(Conforming{});
  EXPECT_FALSE(static_cast<bool>(const_p));

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(static_cast<bool>(pv));

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(static_cast<bool>(pcv));
}

TEST(ReflectionProtocolTest, ConversionToMultipleDistinctTargets) {
  struct Interface {
    explicit operator bool() const noexcept;
    explicit operator int() const noexcept;
  };

  struct Conforming {
    explicit operator bool() const noexcept { return true; }

    explicit operator int() const noexcept { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(static_cast<bool>(p));
  EXPECT_EQ(static_cast<int>(p), 42);
}

struct ImplicitIntConversionInterface {
  operator int() const noexcept;
};

struct ImplicitIntConversionConforming {
  operator int() const noexcept { return 42; }
};

TEST(ReflectionProtocolTest, NonExplicitConversionIsImplicit) {
  using Interface = ImplicitIntConversionInterface;
  using Conforming = ImplicitIntConversionConforming;

  static_assert(std::is_convertible_v<protocol<Interface>, int>);

  protocol<Interface> p(Conforming{});
  int x = p;
  EXPECT_EQ(x, 42);
}

TEST(ReflectionProtocolViewTest, NonExplicitConversionIsImplicit) {
  using Interface = ImplicitIntConversionInterface;
  using Conforming = ImplicitIntConversionConforming;

  static_assert(std::is_convertible_v<protocol_view<Interface>, int>);
  static_assert(std::is_convertible_v<protocol_view<const Interface>, int>);

  Conforming c;
  protocol_view<Interface> pv(c);
  int y = pv;
  EXPECT_EQ(y, 42);

  protocol_view<const Interface> pcv(c);
  int z = pcv;
  EXPECT_EQ(z, 42);
}

TEST(ReflectionProtocolTest, NonNoexceptConversion) {
  struct Interface {
    explicit operator bool() const;
  };

  struct Conforming {
    explicit operator bool() const { return true; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(static_cast<bool>(p));
  static_assert(!noexcept(static_cast<bool>(p)));
}

TEST(ReflectionProtocolTest, ConversionTargetMatchesThroughAlias) {
  using S = std::string;

  struct Interface {
    explicit operator S() const;
  };

  struct Conforming {
    explicit operator std::string() const { return "hello"; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(static_cast<std::string>(p), "hello");
}

// A class type target for `ConversionToClassType`, at namespace scope so its
// mangled name has a real enclosing scope to walk.
struct ConversionTargetWidget {
  int value = 0;
};

TEST(ReflectionProtocolTest, ConversionToClassType) {
  struct Interface {
    explicit operator ConversionTargetWidget() const noexcept;
  };

  struct Conforming {
    explicit operator ConversionTargetWidget() const noexcept {
      return ConversionTargetWidget{42};
    }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(static_cast<ConversionTargetWidget>(p).value, 42);
}

TEST(ReflectionProtocolTest, ConversionAlongsideNamedMember) {
  struct Interface {
    int get() const noexcept;
    explicit operator bool() const noexcept;
  };

  struct Conforming {
    int get() const noexcept { return 42; }

    explicit operator bool() const noexcept { return true; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.get(), 42);
  EXPECT_TRUE(static_cast<bool>(p));
}

// Returns `true` if generating the wrappers of `protocol_view<Interface>`
// throws during constant evaluation. See `conformance_check_rejects` in
// conformance_tests.cc for why this needs P3068 constexpr exceptions.
#ifdef __cpp_constexpr_exceptions
template <typename Interface>
consteval bool view_wrappers_reject() {
  try {
    (void)xyz::detail::generate_member_bases_wrapper<
        ^^Interface, protocol_view<Interface>, xyz::detail::vtable_t<Interface>,
        xyz::detail::const_policy::all_const>();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}
#endif  // __cpp_constexpr_exceptions

// A non-const object prefers a non-const conversion function over a const
// one to a better-matching target. Every wrapper on `protocol_view<I>` is
// const-qualified, so the view would select the other one.
TEST(ReflectionProtocolViewTest, ConversionFunctionsOfMixedConstnessRejected) {
  struct Interface {
    operator int();
    operator long() const;
  };

  struct Conforming {
    operator int() { return 1; }

    operator long() const { return 2; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(view_wrappers_reject<Interface>());
#endif  // __cpp_constexpr_exceptions

  Conforming c;
  long from_object = c;
  EXPECT_EQ(from_object, 1);

  protocol<Interface> p(Conforming{});
  long from_protocol = p;
  EXPECT_EQ(from_protocol, 1);

  protocol_view<const Interface> pcv(c);
  long from_const_view = pcv;
  EXPECT_EQ(from_const_view, 2);
}

TEST(ReflectionProtocolViewTest,
     ExplicitConversionFunctionsOfMixedConstnessRejected) {
  struct Interface {
    explicit operator bool() const;
    operator int();
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(view_wrappers_reject<Interface>());
#endif  // __cpp_constexpr_exceptions
}

// A target with a const and a non-const conversion function counts as
// non-const, as only the non-const one is forwarded by `protocol_view<I>`.
TEST(ReflectionProtocolViewTest, ConstAndNonConstConversionToOneTarget) {
  struct Rejected {
    operator int();
    operator int() const;
    operator long() const;
  };

  struct Interface {
    operator int();
    operator int() const;
    operator long();
  };

  struct Conforming {
    operator int() { return 1; }

    operator int() const { return 2; }

    operator long() { return 3; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(view_wrappers_reject<Rejected>());
  static_assert(!view_wrappers_reject<Interface>());
#endif  // __cpp_constexpr_exceptions

  Conforming c;
  long from_object = c;
  EXPECT_EQ(from_object, 3);

  protocol_view<Interface> pv(c);
  long from_view = pv;
  EXPECT_EQ(from_view, 3);
}

TEST(ReflectionProtocolViewTest, ConversionFunctionsOfOneConstnessAccepted) {
  struct ConstInterface {
    operator int() const;
    operator long() const;
  };

  struct NonConstInterface {
    operator int();
    operator long();
  };

  struct Conforming {
    operator int() const { return 1; }

    operator long() const { return 2; }
  };

  struct NonConstConforming {
    operator int() { return 1; }

    operator long() { return 2; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(!view_wrappers_reject<ConstInterface>());
  static_assert(!view_wrappers_reject<NonConstInterface>());
#endif  // __cpp_constexpr_exceptions

  Conforming c;
  protocol_view<ConstInterface> pv(c);
  long from_view = pv;
  EXPECT_EQ(from_view, 2);

  NonConstConforming non_const_c;
  protocol_view<NonConstInterface> non_const_pv(non_const_c);
  long from_non_const_view = non_const_pv;
  EXPECT_EQ(from_non_const_view, 2);
}

TEST(ReflectionProtocolTest, OperatorPlus) {
  struct Interface {
    int operator+(int rhs) const noexcept;
    int operator+(int rhs) noexcept;
  };

  struct Conforming {
    int operator+(int rhs) const noexcept { return rhs; }

    int operator+(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p + 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv + 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv + 3, 3);
}

TEST(ReflectionProtocolTest, OperatorPlusUnary) {
  struct Interface {
    int operator+() const noexcept;
    int operator+() noexcept;
  };

  struct Conforming {
    int operator+() const noexcept { return 3; }

    int operator+() noexcept { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(+p, 42);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(+pv, 42);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(+pcv, 3);
}

TEST(ReflectionProtocolTest, OperatorMinus) {
  struct Interface {
    int operator-(int rhs) const noexcept;
    int operator-(int rhs) noexcept;
  };

  struct Conforming {
    int operator-(int rhs) const noexcept { return rhs; }

    int operator-(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p - 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv - 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv - 3, 3);
}

TEST(ReflectionProtocolTest, OperatorMinusUnary) {
  struct Interface {
    int operator-() const noexcept;
    int operator-() noexcept;
  };

  struct Conforming {
    int operator-() const noexcept { return 3; }

    int operator-() noexcept { return 42; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(-p, 42);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(-pv, 42);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(-pcv, 3);
}

TEST(ReflectionProtocolTest, OperatorSlash) {
  struct Interface {
    int operator/(int rhs) const noexcept;
    int operator/(int rhs) noexcept;
  };

  struct Conforming {
    int operator/(int rhs) const noexcept { return rhs; }

    int operator/(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p / 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv / 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv / 3, 3);
}

TEST(ReflectionProtocolTest, OperatorPercent) {
  struct Interface {
    int operator%(int rhs) const noexcept;
    int operator%(int rhs) noexcept;
  };

  struct Conforming {
    int operator%(int rhs) const noexcept { return rhs; }

    int operator%(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p % 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv % 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv % 3, 3);
}

TEST(ReflectionProtocolTest, OperatorCaret) {
  struct Interface {
    int operator^(int rhs) const noexcept;
    int operator^(int rhs) noexcept;
  };

  struct Conforming {
    int operator^(int rhs) const noexcept { return rhs; }

    int operator^(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p ^ 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv ^ 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv ^ 3, 3);
}

TEST(ReflectionProtocolTest, OperatorAmpersand) {
  struct Interface {
    int operator&(int rhs) const noexcept;
    int operator&(int rhs) noexcept;
  };

  struct Conforming {
    int operator&(int rhs) const noexcept { return rhs; }

    int operator&(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p & 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv & 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv & 3, 3);
}

TEST(ReflectionProtocolTest, OperatorPipe) {
  struct Interface {
    int operator|(int rhs) const noexcept;
    int operator|(int rhs) noexcept;
  };

  struct Conforming {
    int operator|(int rhs) const noexcept { return rhs; }

    int operator|(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p | 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv | 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv | 3, 3);
}

// `+`, `-` and `*` mangle differently as unary than as binary; a test in
// name_mangling_tests.cc checks that at the mangling level. This checks
// the same split through `operator_overload_set` and the vtable, by
// declaring both arities of each on one interface and dispatching both
// through a protocol.
TEST(ReflectionProtocolTest, UnaryAndBinaryFormsOfAmbiguousOperatorsCoexist) {
  struct Interface {
    int operator+() const noexcept;
    int operator+(int rhs) const noexcept;
    int operator-() const noexcept;
    int operator-(int rhs) const noexcept;
    int operator*() const noexcept;
    int operator*(int rhs) const noexcept;
  };

  struct Conforming {
    int operator+() const noexcept { return 1; }

    int operator+(int rhs) const noexcept { return 10 + rhs; }

    int operator-() const noexcept { return 2; }

    int operator-(int rhs) const noexcept { return 20 + rhs; }

    int operator*() const noexcept { return 3; }

    int operator*(int rhs) const noexcept { return 30 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(+p, 1);
  EXPECT_EQ(p + 5, 15);
  EXPECT_EQ(-p, 2);
  EXPECT_EQ(p - 5, 25);
  EXPECT_EQ(*p, 3);
  EXPECT_EQ(p * 5, 35);
}

TEST(ReflectionProtocolTest, OperatorPlusEquals) {
  struct Interface {
    int operator+=(int rhs) const noexcept;
    int operator+=(int rhs) noexcept;
  };

  struct Conforming {
    int operator+=(int rhs) const noexcept { return rhs; }

    int operator+=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p += 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv += 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv += 3, 3);
}

TEST(ReflectionProtocolTest, OperatorMinusEquals) {
  struct Interface {
    int operator-=(int rhs) const noexcept;
    int operator-=(int rhs) noexcept;
  };

  struct Conforming {
    int operator-=(int rhs) const noexcept { return rhs; }

    int operator-=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p -= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv -= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv -= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorStarEquals) {
  struct Interface {
    int operator*=(int rhs) const noexcept;
    int operator*=(int rhs) noexcept;
  };

  struct Conforming {
    int operator*=(int rhs) const noexcept { return rhs; }

    int operator*=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p *= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv *= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv *= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorSlashEquals) {
  struct Interface {
    int operator/=(int rhs) const noexcept;
    int operator/=(int rhs) noexcept;
  };

  struct Conforming {
    int operator/=(int rhs) const noexcept { return rhs; }

    int operator/=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p /= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv /= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv /= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorPercentEquals) {
  struct Interface {
    int operator%=(int rhs) const noexcept;
    int operator%=(int rhs) noexcept;
  };

  struct Conforming {
    int operator%=(int rhs) const noexcept { return rhs; }

    int operator%=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p %= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv %= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv %= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorCaretEquals) {
  struct Interface {
    int operator^=(int rhs) const noexcept;
    int operator^=(int rhs) noexcept;
  };

  struct Conforming {
    int operator^=(int rhs) const noexcept { return rhs; }

    int operator^=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p ^= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv ^= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv ^= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorAmpersandEquals) {
  struct Interface {
    int operator&=(int rhs) const noexcept;
    int operator&=(int rhs) noexcept;
  };

  struct Conforming {
    int operator&=(int rhs) const noexcept { return rhs; }

    int operator&=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p &= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv &= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv &= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorPipeEquals) {
  struct Interface {
    int operator|=(int rhs) const noexcept;
    int operator|=(int rhs) noexcept;
  };

  struct Conforming {
    int operator|=(int rhs) const noexcept { return rhs; }

    int operator|=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p |= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv |= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv |= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorLogicalAnd) {
  struct Interface {
    bool operator&&(int rhs) const noexcept;
    bool operator&&(int rhs) noexcept;
  };

  struct Conforming {
    bool operator&&(int) const noexcept { return false; }

    bool operator&&(int) noexcept { return true; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p && 3);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(pv && 3);

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(pcv && 3);
}

TEST(ReflectionProtocolTest, OperatorLogicalOr) {
  struct Interface {
    bool operator||(int rhs) const noexcept;
    bool operator||(int rhs) noexcept;
  };

  struct Conforming {
    bool operator||(int) const noexcept { return false; }

    bool operator||(int) noexcept { return true; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p || 3);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(pv || 3);

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(pcv || 3);
}

TEST(ReflectionProtocolTest, OperatorLeftShift) {
  struct Interface {
    int operator<<(int rhs) const noexcept;
    int operator<<(int rhs) noexcept;
  };

  struct Conforming {
    int operator<<(int rhs) const noexcept { return rhs; }

    int operator<<(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p << 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv << 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv << 3, 3);
}

TEST(ReflectionProtocolTest, OperatorRightShift) {
  struct Interface {
    int operator>>(int rhs) const noexcept;
    int operator>>(int rhs) noexcept;
  };

  struct Conforming {
    int operator>>(int rhs) const noexcept { return rhs; }

    int operator>>(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p >> 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv >> 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv >> 3, 3);
}

TEST(ReflectionProtocolTest, OperatorLeftShiftEquals) {
  struct Interface {
    int operator<<=(int rhs) const noexcept;
    int operator<<=(int rhs) noexcept;
  };

  struct Conforming {
    int operator<<=(int rhs) const noexcept { return rhs; }

    int operator<<=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p <<= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv <<= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv <<= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorRightShiftEquals) {
  struct Interface {
    int operator>>=(int rhs) const noexcept;
    int operator>>=(int rhs) noexcept;
  };

  struct Conforming {
    int operator>>=(int rhs) const noexcept { return rhs; }

    int operator>>=(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p >>= 3, 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv >>= 3, 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(pcv >>= 3, 3);
}

TEST(ReflectionProtocolTest, OperatorPlusPlus) {
  struct Interface {
    int operator++() const noexcept;
    int operator++() noexcept;
    int operator++(int) const noexcept;
    int operator++(int) noexcept;
  };

  struct Conforming {
    int operator++() const noexcept { return 1; }

    int operator++() noexcept { return 42; }

    int operator++(int) const noexcept { return 2; }

    int operator++(int) noexcept { return 43; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(++p, 42);
  EXPECT_EQ(p++, 43);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(++pv, 42);
  EXPECT_EQ(pv++, 43);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(++pcv, 1);
  EXPECT_EQ(pcv++, 2);
}

TEST(ReflectionProtocolTest, OperatorMinusMinus) {
  struct Interface {
    int operator--() const noexcept;
    int operator--() noexcept;
    int operator--(int) const noexcept;
    int operator--(int) noexcept;
  };

  struct Conforming {
    int operator--() const noexcept { return 1; }

    int operator--() noexcept { return 42; }

    int operator--(int) const noexcept { return 2; }

    int operator--(int) noexcept { return 43; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(--p, 42);
  EXPECT_EQ(p--, 43);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(--pv, 42);
  EXPECT_EQ(pv--, 43);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ(--pcv, 1);
  EXPECT_EQ(pcv--, 2);
}

TEST(ReflectionProtocolTest, OperatorComma) {
  struct Interface {
    int operator,(int rhs) const noexcept;
    int operator,(int rhs) noexcept;
  };

  struct Conforming {
    int operator,(int rhs) const noexcept { return rhs; }

    int operator,(int rhs) noexcept { return 42 + rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ((p, 3), 45);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ((pv, 3), 45);

  protocol_view<const Interface> pcv(c);
  EXPECT_EQ((pcv, 3), 3);
}

TEST(ReflectionProtocolTest, OperatorEqualsEquals) {
  struct Interface {
    bool operator==(int rhs) const noexcept;
    bool operator==(int rhs) noexcept;
  };

  struct Conforming {
    bool operator==(int rhs) const noexcept { return rhs == 1; }

    bool operator==(int rhs) noexcept { return rhs == 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p == 2);
  EXPECT_FALSE(p == 1);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(pv == 2);
  EXPECT_FALSE(pv == 1);

  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE(pcv == 1);
  EXPECT_FALSE(pcv == 2);
}

TEST(ReflectionProtocolTest, OperatorNotEquals) {
  struct Interface {
    bool operator!=(int rhs) const noexcept;
    bool operator!=(int rhs) noexcept;
  };

  struct Conforming {
    bool operator!=(int rhs) const noexcept { return rhs != 1; }

    bool operator!=(int rhs) noexcept { return rhs != 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p != 1);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(pv != 1);

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(pcv != 1);
}

TEST(ReflectionProtocolTest, OperatorLess) {
  struct Interface {
    bool operator<(int rhs) const noexcept;
    bool operator<(int rhs) noexcept;
  };

  struct Conforming {
    bool operator<(int rhs) const noexcept { return 1 < rhs; }

    bool operator<(int rhs) noexcept { return 10 < rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_FALSE(p < 5);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_FALSE(pv < 5);

  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE(pcv < 5);
}

TEST(ReflectionProtocolTest, OperatorLessEquals) {
  struct Interface {
    bool operator<=(int rhs) const noexcept;
    bool operator<=(int rhs) noexcept;
  };

  struct Conforming {
    bool operator<=(int rhs) const noexcept { return 1 <= rhs; }

    bool operator<=(int rhs) noexcept { return 10 <= rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_FALSE(p <= 5);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_FALSE(pv <= 5);

  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE(pcv <= 5);
}

TEST(ReflectionProtocolTest, OperatorGreater) {
  struct Interface {
    bool operator>(int rhs) const noexcept;
    bool operator>(int rhs) noexcept;
  };

  struct Conforming {
    bool operator>(int rhs) const noexcept { return 1 > rhs; }

    bool operator>(int rhs) noexcept { return 10 > rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p > 5);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(pv > 5);

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(pcv > 5);
}

TEST(ReflectionProtocolTest, OperatorGreaterEquals) {
  struct Interface {
    bool operator>=(int rhs) const noexcept;
    bool operator>=(int rhs) noexcept;
  };

  struct Conforming {
    bool operator>=(int rhs) const noexcept { return 1 >= rhs; }

    bool operator>=(int rhs) noexcept { return 10 >= rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p >= 5);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE(pv >= 5);

  protocol_view<const Interface> pcv(c);
  EXPECT_FALSE(pcv >= 5);
}

TEST(ReflectionProtocolTest, OperatorSpaceship) {
  struct Interface {
    std::strong_ordering operator<=>(int rhs) const noexcept;
    std::strong_ordering operator<=>(int rhs) noexcept;
  };

  struct Conforming {
    std::strong_ordering operator<=>(int rhs) const noexcept {
      return 1 <=> rhs;
    }

    std::strong_ordering operator<=>(int rhs) noexcept { return 10 <=> rhs; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE((p <=> 5) == std::strong_ordering::greater);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_TRUE((pv <=> 5) == std::strong_ordering::greater);

  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE((pcv <=> 5) == std::strong_ordering::less);
}

TEST(ReflectionProtocolTest, ComparisonOperatorsSupportRewrittenCandidates) {
  struct Interface {
    bool operator==(int rhs) const;
    std::strong_ordering operator<=>(int rhs) const;
  };

  struct Conforming {
    int value;

    bool operator==(int rhs) const { return value == rhs; }

    std::strong_ordering operator<=>(int rhs) const { return value <=> rhs; }
  };

  protocol<Interface> p(Conforming{3});
  EXPECT_TRUE(p == 3);
  EXPECT_TRUE(3 == p);
  EXPECT_TRUE(p != 4);
  EXPECT_TRUE(4 != p);
  EXPECT_TRUE(p < 4);
  EXPECT_TRUE(p <= 3);
  EXPECT_TRUE(p > 2);
  EXPECT_TRUE(p >= 3);
  EXPECT_TRUE(2 < p);
  EXPECT_TRUE(4 > p);

  const Conforming c{3};
  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE(pcv == 3);
  EXPECT_TRUE(3 == pcv);
  EXPECT_TRUE(pcv != 4);
  EXPECT_TRUE(4 != pcv);
  EXPECT_TRUE(pcv < 4);
  EXPECT_TRUE(pcv <= 3);
  EXPECT_TRUE(pcv > 2);
  EXPECT_TRUE(pcv >= 3);
  EXPECT_TRUE(2 < pcv);
  EXPECT_TRUE(4 > pcv);
}

TEST(ReflectionProtocolTest, ComparisonOperatorIsNoexceptWhenInterfaceIs) {
  struct Interface {
    bool operator==(int rhs) const noexcept;
    bool operator<(int rhs) const;
  };

  struct Conforming {
    bool operator==(int rhs) const noexcept { return rhs == 1; }

    bool operator<(int rhs) const { return rhs < 1; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p == 1);
  static_assert(noexcept(p == 1));
  static_assert(!noexcept(p < 1));
}

struct ComparisonOperandWidget {
  int value;
};

TEST(ReflectionProtocolTest, ComparisonWithClassTypeOperand) {
  struct Interface {
    bool operator==(const ComparisonOperandWidget& rhs) const;
  };

  struct Conforming {
    bool operator==(const ComparisonOperandWidget& rhs) const {
      return rhs.value == 42;
    }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_TRUE(p == ComparisonOperandWidget{42});
  EXPECT_FALSE(p == ComparisonOperandWidget{7});

  Conforming c;
  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE(pcv == ComparisonOperandWidget{42});
  EXPECT_FALSE(pcv == ComparisonOperandWidget{7});
}

TEST(ReflectionProtocolTest, ComparisonWithStringViewAndSentinelOperands) {
  struct Interface {
    bool operator==(std::string_view rhs) const;
    bool operator==(std::default_sentinel_t) const;
  };

  struct Conforming {
    std::string_view name;

    bool operator==(std::string_view rhs) const { return name == rhs; }

    bool operator==(std::default_sentinel_t) const { return name.empty(); }
  };

  protocol<Interface> p(Conforming{"id"});
  EXPECT_TRUE(p == "id");
  EXPECT_TRUE("id" == p);
  EXPECT_TRUE(p != "other");
  EXPECT_TRUE(p != std::default_sentinel);
  EXPECT_TRUE(std::default_sentinel != p);

  const Conforming c{""};
  protocol_view<const Interface> pcv(c);
  EXPECT_TRUE(pcv == "");
  EXPECT_TRUE(std::default_sentinel == pcv);
}

TEST(ReflectionProtocolTest, RefQualifiedCallOperators) {
  struct Interface {
    int operator()() &;
    int operator()() const&;
    int operator()() &&;
  };

  struct Conforming {
    int operator()() & { return 1; }

    int operator()() const& { return 2; }

    int operator()() && { return 3; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p(), 1);

  const protocol<Interface>& const_p = p;
  EXPECT_EQ(const_p(), 2);

  EXPECT_EQ(std::move(p)(), 3);

  Conforming c;
  protocol_view<Interface> pv(c);
  EXPECT_EQ(pv(), 1);
  EXPECT_EQ(protocol_view<Interface>(c)(), 1);
}

TEST(ReflectionProtocolTest, RefQualifiedBinaryOperator) {
  struct Interface {
    int operator+(int) const&;
    int operator+(int) &&;
  };

  struct Conforming {
    int operator+(int value) const& { return value + 1; }

    int operator+(int value) && { return value + 2; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p + 10, 11);
  EXPECT_EQ(std::move(p) + 10, 12);
}

TEST(ReflectionProtocolTest, RvalueQualifiedConversion) {
  struct Interface {
    explicit operator std::string() &&;
  };

  struct Conforming {
    std::string text;

    explicit operator std::string() && { return std::move(text); }
  };

  static_assert(std::is_constructible_v<std::string, protocol<Interface>>);
  static_assert(!std::is_constructible_v<std::string, protocol<Interface>&>);

  protocol<Interface> p(Conforming{"converted"});
  EXPECT_EQ(static_cast<std::string>(std::move(p)), "converted");
}
}  // namespace

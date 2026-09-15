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
}  // namespace

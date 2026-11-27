// Development tests for faster rebuild-run cycles.

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

namespace xyz::tests {
namespace {

enum class ReferenceQualifier {
  None,
  Const,
  LValue,
  ConstLValue,
  RValue,
  ConstRValue,
};

TEST(ReflectionProtocolTest, ReferenceQualifers_NoneAndConst) {
  struct Interface {
    ReferenceQualifier fn();
    ReferenceQualifier fn() const;
  };

  struct Conforming {
    ReferenceQualifier fn() { return ReferenceQualifier::None; }

    ReferenceQualifier fn() const { return ReferenceQualifier::Const; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.fn(), ReferenceQualifier::None);
  EXPECT_EQ(std::as_const(p).fn(), ReferenceQualifier::Const);
}

TEST(ReflectionProtocolTest, ReferenceQualifers_LValueAndRValue) {
  struct Interface {
    ReferenceQualifier fn() &;
    ReferenceQualifier fn() const &;
    ReferenceQualifier fn() &&;
    ReferenceQualifier fn() const &&;
  };

  struct Conforming {
    ReferenceQualifier fn() & { return ReferenceQualifier::LValue; }

    ReferenceQualifier fn() const & { return ReferenceQualifier::ConstLValue; }

    ReferenceQualifier fn() && { return ReferenceQualifier::RValue; }

    ReferenceQualifier fn() const && { return ReferenceQualifier::ConstRValue; }
  };

  protocol<Interface> p(Conforming{});
  EXPECT_EQ(p.fn(), ReferenceQualifier::LValue);
  EXPECT_EQ(std::as_const(p).fn(), ReferenceQualifier::ConstLValue);
  EXPECT_EQ(std::move(p).fn(), ReferenceQualifier::RValue);
  EXPECT_EQ(std::move(std::as_const(p)).fn(), ReferenceQualifier::ConstRValue);
}

}  // namespace
}  // namespace xyz::tests

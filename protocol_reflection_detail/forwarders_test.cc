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
#include "protocol_reflection_detail/forwarders.hxx"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>

namespace {

using xyz::reflection_detail::forwarder_base;
using xyz::reflection_detail::forwarders_type;

// Names only: an interface never instantiated, just reflected upon so
// forwarder_base has something to name each synthesized base after.
struct NamingFixture {
  std::string greet(std::string_view) const;
  std::string shout(std::string_view) const;
  int count() const;
};

// --- Three members: GreetBase/ShoutBase/CountBase synthesized via
// forwarder_base instead of hand-written.

struct ThreeMemberOwner;

struct GreetWrapper {
  std::string operator()(std::string_view visitor) const;
};

struct ShoutWrapper {
  std::string operator()(std::string_view visitor) const;
};

struct CountWrapper {
  int operator()() const;
};

using GreetBase = forwarder_base<GreetWrapper, ^^NamingFixture::greet>;
using ShoutBase = forwarder_base<ShoutWrapper, ^^NamingFixture::shout>;
using CountBase = forwarder_base<CountWrapper, ^^NamingFixture::count>;

using ThreeMemberForwarders =
    typename[:forwarders_type({^^GreetBase, ^^ShoutBase, ^^CountBase}):];

struct ThreeMemberOwner : ThreeMemberForwarders {
  std::string name = "World";
};

std::string GreetWrapper::operator()(std::string_view visitor) const {
  const auto* base =
      static_cast<const GreetBase*>(static_cast<const void*>(this));
  const auto* owner = static_cast<const ThreeMemberOwner*>(base);
  return "Hello, " + std::string(visitor) + ", from " + owner->name + "!";
}

std::string ShoutWrapper::operator()(std::string_view visitor) const {
  const auto* base =
      static_cast<const ShoutBase*>(static_cast<const void*>(this));
  const auto* owner = static_cast<const ThreeMemberOwner*>(base);
  return std::string(visitor) + ", " + owner->name + " SHOUTS HELLO!";
}

int CountWrapper::operator()() const {
  const auto* base =
      static_cast<const CountBase*>(static_cast<const void*>(this));
  const auto* owner = static_cast<const ThreeMemberOwner*>(base);
  return static_cast<int>(owner->name.size());
}

TEST(Forwarders, EachSynthesizedBaseIsARealBaseOfTheOwner) {
  static_assert(std::is_base_of_v<GreetBase, ThreeMemberOwner>);
  static_assert(std::is_base_of_v<ShoutBase, ThreeMemberOwner>);
  static_assert(std::is_base_of_v<CountBase, ThreeMemberOwner>);
}

TEST(Forwarders, InheritedLookupCallsEachNamedForwarderWithNoSplicing) {
  ThreeMemberOwner owner;
  owner.name = "Owner";

  EXPECT_EQ(owner.greet("Reader"), "Hello, Reader, from Owner!");
  EXPECT_EQ(owner.shout("Reader"), "Reader, Owner SHOUTS HELLO!");
  EXPECT_EQ(owner.count(), 5);
}

TEST(Forwarders, EmptyBasesAddNoStorageBeyondTheOwnersOwnMembers) {
  // Three no_unique_address forwarder bases, none adding size: sizeof
  // matches a bare std::string member.
  EXPECT_EQ(sizeof(ThreeMemberOwner), sizeof(std::string));
}

// --- One member.

struct OneMemberOwner;

struct OneWrapper {
  int operator()() const;
};

using OneBase = forwarder_base<OneWrapper, ^^NamingFixture::count>;
using OneMemberForwarders = typename[:forwarders_type({^^OneBase}):];

struct OneMemberOwner : OneMemberForwarders {
  int value = 7;
};

int OneWrapper::operator()() const {
  const auto* base =
      static_cast<const OneBase*>(static_cast<const void*>(this));
  const auto* owner = static_cast<const OneMemberOwner*>(base);
  return owner->value;
}

TEST(Forwarders, OneMemberIsABaseAndAddsNoStorage) {
  static_assert(std::is_base_of_v<OneBase, OneMemberOwner>);
  static_assert(sizeof(OneMemberOwner) == sizeof(int));
}

TEST(Forwarders, OneMemberDispatchesCorrectly) {
  OneMemberOwner owner;
  EXPECT_EQ(owner.count(), 7);
}

// --- Zero members: forwarders<> is a valid, empty combinator.

using ZeroMemberForwarders = typename[:forwarders_type({}):];

struct ZeroMemberOwner : ZeroMemberForwarders {
  int value = 0;
};

TEST(Forwarders, ZeroMembersIsAnEmptyTypeAddingNoStorage) {
  static_assert(std::is_empty_v<ZeroMemberForwarders>);
  static_assert(sizeof(ZeroMemberOwner) == sizeof(int));
}

}  // namespace

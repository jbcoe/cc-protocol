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

// The vanishing `this` pointer.
//
// This technique is adopted from https://ryanjk5.github.io/posts/rjk-duck/.
//
// A technique for giving a data member ordinary call syntax, built up in
// four sections, read top to bottom:
//   1. The offset-zero layout guarantee.
//   2. Recovering the owner pointer.
//   3. Ordinary call syntax from a data member.
//   4. What breaks when offset zero is violated, and the guard against it.
//
// Each section defines its own small Owner/Wrapper pair, scoped to a named
// namespace, since each section's version is deliberately minimal for what
// it's teaching rather than reusing the previous section's exact shape.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

// ---------------------------------------------------------------------------
// 1. The offset-zero layout guarantee.
//
// A class's first non-static data member starts at the same address as the
// object that contains it. This is called offset zero.
// ---------------------------------------------------------------------------
namespace section_1 {

struct Wrapper {
  int tag = 0;
};

struct Owner {
  Wrapper wrapper;  // first data member
  int other_data = 0;
};

TEST(ThisPointerOffsetZero, FirstMemberSharesAddress) {
  Owner owner;

  EXPECT_EQ(static_cast<void*>(&owner), static_cast<void*>(&owner.wrapper));
  EXPECT_EQ(offsetof(Owner, wrapper), 0u);

  // other_data is not first, so it does not share the owner's address.
  EXPECT_NE(static_cast<void*>(&owner), static_cast<void*>(&owner.other_data));
  EXPECT_NE(offsetof(Owner, other_data), 0u);
}

// An empty Wrapper, stored as a data member rather than inherited from,
// still costs no extra storage: [[no_unique_address]] is the data-member
// counterpart of the empty base optimization, and lets an empty member
// overlap entirely with the owner's own storage.
struct EmptyWrapper {};

struct OwnerWithEmptyWrapper {
  [[no_unique_address]] EmptyWrapper wrapper;
  int payload = 0;
};

TEST(ThisPointerOffsetZero, EmptyWrapperAddsNoStorage) {
  EXPECT_EQ(sizeof(OwnerWithEmptyWrapper), sizeof(int));
  EXPECT_EQ(offsetof(OwnerWithEmptyWrapper, wrapper), 0u);
}

}  // namespace section_1

// ---------------------------------------------------------------------------
// 2. Recovering the owner pointer, "the vanishing this pointer" itself.
//
// Because a Wrapper sitting at offset zero of an Owner shares the Owner's
// address (section 1), a method defined on Wrapper can compute a pointer to
// its owner just by casting its own `this`, without Wrapper storing a
// back-pointer anywhere.
// ---------------------------------------------------------------------------
namespace section_2 {

struct Wrapper {
  int recover_owner_value() const;
};

struct Owner {
  Wrapper wrapper;
  int value = 0;
};

int Wrapper::recover_owner_value() const {
  const auto* owner = static_cast<const Owner*>(static_cast<const void*>(this));
  return owner->value;
}

TEST(ThisPointerRecoverOwner, ReachesOwnersData) {
  Owner owner;
  owner.value = 42;

  EXPECT_EQ(owner.wrapper.recover_owner_value(), 42);
}

TEST(ThisPointerRecoverOwner, NoExtraStorage) {
  // sizeof(Wrapper) == 1 (the minimum for any object, empty or not), not 8
  // (a pointer's worth): confirms no back-pointer is stored.
  EXPECT_EQ(sizeof(Wrapper), 1u);
}

}  // namespace section_2

// ---------------------------------------------------------------------------
// 3. Ordinary call syntax from a data member.
//
// Give Wrapper an operator() instead of an oddly-named method, make it a
// data member of Owner instead of a method, and owner.member_name(...)
// becomes an ordinary function call, even though member_name is a value of
// class type rather than a function. Combined with section 2's owner
// recovery, that call can reach and mutate the real owner.
// ---------------------------------------------------------------------------
namespace section_3 {

struct GreetWrapper {
  std::string operator()(std::string_view visitor) const;  // defined below
};

struct Owner {
  GreetWrapper greet;
  std::string name = "World";
};

std::string GreetWrapper::operator()(std::string_view visitor) const {
  const auto* owner = static_cast<const Owner*>(static_cast<const void*>(this));
  return "Hello, " + std::string(visitor) + ", from " + owner->name + "!";
}

TEST(ThisPointerCallSyntax, BehavesAsOrdinaryCall) {
  Owner owner;
  owner.name = "Owner";

  EXPECT_EQ(owner.greet("Reader"), "Hello, Reader, from Owner!");
}

TEST(ThisPointerCallSyntax, IsADataMemberNotMethod) {
  Owner owner;

  // owner.greet() calls that member's operator().
  static_assert(std::is_class_v<decltype(owner.greet)>);
  static_assert(!std::is_function_v<decltype(owner.greet)>);
}

}  // namespace section_3

// ---------------------------------------------------------------------------
// 4. What breaks when offset zero is violated, and the guard against it.
//
// Everything in sections 2-3 depends on Wrapper being Owner's first data
// member. This section shows what happens if it isn't: the address a
// Wrapper method would recover as "the owner" no longer matches the real
// owner's address, off by exactly sizeof(preceding_member): enough to land
// on preceding_member's storage instead of Owner's real first member.
// Every check below computes and compares addresses only; none
// dereferences the miscomputed pointer, since actually reading through it
// would be undefined behavior.
// ---------------------------------------------------------------------------
namespace section_4 {

struct Wrapper {
  int tag = 0;
};

// BadOwner puts another member before wrapper, so wrapper is no longer at
// offset zero.
struct BadOwner {
  int preceding_member = 0;
  Wrapper wrapper;
};

TEST(ThisPointerOffsetViolated, AddressNoLongerMatches) {
  BadOwner owner;

  EXPECT_NE(static_cast<void*>(&owner), static_cast<void*>(&owner.wrapper));
  EXPECT_NE(offsetof(BadOwner, wrapper), 0u);
}

TEST(ThisPointerOffsetViolated, MisrecoveredAddressLandsOnPrecedingMember) {
  BadOwner owner;

  // wrong_owner is the address a Wrapper method would recover as "the
  // owner." Only the distance to real_owner is computed; that address is
  // never dereferenced.
  const auto* wrong_owner =
      static_cast<const char*>(static_cast<const void*>(&owner.wrapper));
  const auto* real_owner =
      static_cast<const char*>(static_cast<const void*>(&owner));
  EXPECT_EQ(wrong_owner - real_owner,
            static_cast<std::ptrdiff_t>(sizeof(owner.preceding_member)));
}

// GoodOwner asserts its layout precondition instead of assuming it: a
// static_assert here fails at compile time if a future change moves wrapper
// away from being GoodOwner's first member.
struct GoodOwner {
  Wrapper wrapper;
  int payload = 0;
};

static_assert(offsetof(GoodOwner, wrapper) == 0,
              "wrapper must be GoodOwner's first data member for the "
              "vanishing-this-pointer cast to be valid");

TEST(ThisPointerOffsetViolated, GuardConfirmsGoodCase) {
  EXPECT_EQ(offsetof(GoodOwner, wrapper), 0u);
}

}  // namespace section_4

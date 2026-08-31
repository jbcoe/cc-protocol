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

// An introduction to the vanishing-this technique adopted from
// https://ryanjk5.github.io/posts/rjk-duck/.

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

// A pointer to member data at offset zero can be cast to a pointer to the
// class.

namespace xyz::tutorials::cast_member_to_parent {

TEST(TutorialsVanishingThis, CastMemberToParent) {
  struct A {
    int x;
    std::string name;
  };

  A a{.x = 5, .name = "Ozymandias"};
  int* x_ptr = &a.x;
  auto* a_ptr = reinterpret_cast<A*>(x_ptr);
  EXPECT_EQ(a_ptr->name, "Ozymandias");
}

}  // namespace xyz::tutorials::cast_member_to_parent

// operator() can be invoked on member data for function-call-like syntax.

namespace xyz::tutorials::call_member_data {

TEST(TutorialsVanishingThis, CallMemberData) {
  class Callable {
   public:
    int operator()(int x) const { return x * 2; }
  };

  class A {
   public:
    [[no_unique_address]] Callable fn;
  };

  A a;
  EXPECT_EQ(a.fn(5), 10);
}

}  // namespace xyz::tutorials::call_member_data

// A callable member with no offset can be cast to the parent class and access
// parent class member data.
namespace xyz::tutorials::access_parent_from_callable_member {

TEST(TutorialsVanishingThis, ParentClassAccessFromMemberDataCall) {
  class A {
    struct Callable {
      int operator()(int x) const {
        // Two objects are pointer-interconvertible if one is a standard-layout
        // class object and the other is the first non-static data member of
        // that object.
        // https://eel.is/c++draft/basic.compound [7.3]
        static_assert(offsetof(A, fn_) == 0);
        static_assert(std::is_standard_layout_v<A>);
        int value = reinterpret_cast<const A*>(this)->value_;
        return x + value;
      }
    };

   public:
    [[no_unique_address]] Callable fn_;
    int value_;

    A(int value) : value_(value) {}
  };

  // Use of `[[no_unique_address]]` ensures that the `Callable` member has no
  // effect on size.
  static_assert(sizeof(A) == sizeof(int));

  A a(3);
  EXPECT_EQ(a.fn_(5), 8);
}
}  // namespace xyz::tutorials::access_parent_from_callable_member

// Wrapping each callable in its own base struct lets several named members use
// the vanishing-this trick and access parent class member data.

namespace xyz::tutorials::access_parent_from_multiple_callable_members {

struct Add {
  int operator()(int x) const;
};

struct AddBase {
  [[no_unique_address]] Add add;
};

struct Multiply {
  int operator()(int x) const;
};

struct MultiplyBase {
  [[no_unique_address]] Multiply multiply;
};

struct A : AddBase, MultiplyBase {
  int value_;

  A(int value) : value_(value) {}
};

// `operator()` for `Add` and `Multiply` must be defined out of line: the
// `reinterpret_cast` and `static_cast` require `AddBase`, `MultiplyBase` and
// `A` to be complete types.

int Add::operator()(int x) const {
  static_assert(offsetof(AddBase, add) == 0);
  static_assert(std::is_standard_layout_v<AddBase>);
  const auto* base = reinterpret_cast<const AddBase*>(this);
  const auto* owner = static_cast<const A*>(base);
  return x + owner->value_;
}

int Multiply::operator()(int x) const {
  static_assert(offsetof(MultiplyBase, multiply) == 0);
  static_assert(std::is_standard_layout_v<MultiplyBase>);
  const auto* base = reinterpret_cast<const MultiplyBase*>(this);
  const auto* owner = static_cast<const A*>(base);
  return x * owner->value_;
}

TEST(TutorialsVanishingThis, ParentClassAccessFromMultipleMemberDataCalls) {
  // [[no_unique_address]] and the empty base class optimisation ensure that
  // inheriting from `AddBase` and `MultiplyBase` has no effect on size.
  // Calling `operator()` for `add` or `multiply` has the same syntax as
  // a member function call.
  static_assert(sizeof(A) == sizeof(int));

  A a(3);
  EXPECT_EQ(a.add(5), 8);
  EXPECT_EQ(a.multiply(5), 15);
}

}  // namespace xyz::tutorials::access_parent_from_multiple_callable_members

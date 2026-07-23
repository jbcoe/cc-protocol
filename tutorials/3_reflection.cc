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

// C++26 reflection, from std::meta::info to synthesizing a type at compile
// time.
//
// Seven sections, read top to bottom, each building on the primitives the
// previous one introduced:
//   1. What std::meta::info is.
//   2. Splicing an info back into a type.
//   3. Splicing an info as a call target.
//   4. Enumerating a type's members.
//   5. Writing your own consteval helpers about a member.
//   6. Synthesizing a type's data members with define_aggregate.
//   7. Enum reflection: enum_to_string with template for.

#include <gtest/gtest.h>

#include <meta>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// 1. What std::meta::info is.
//
// C++26 reflection (P2996) lets code ask questions about the program at
// compile time, such as what a type's members are or whether a function is
// const, and so on. The reflection operator ^^ takes something in the program
// (a type, a variable, a function, a member...) and lifts it into a
// std::meta::info value: an opaque, compile-time value describing that
// thing, distinct from the thing itself (^^int is a value you can pass
// around, compare, and query; it is not int). This section only
// establishes that fact; turning an info back into usable code
// ("splicing") is sections 2 and 3.
// ---------------------------------------------------------------------------
namespace section_1 {

struct Point {
  int x = 0;
  int y = 0;
};

TEST(ReflectionMetaInfo, SameEntityEqualValues) {
  constexpr std::meta::info reflection_of_int = ^^int;

  // ^^int, evaluated twice, describes the same entity both times.
  static_assert(reflection_of_int == ^^int);

  // Different entities reflect to different, unequal info values.
  static_assert(^^int != ^^double);
  static_assert(^^Point != ^^int);

  // std::meta::info values are ordinary compile-time values: they can be
  // stored in constexpr variables and compared with ==, exactly like any
  // other type in this respect.
}

// ^^ isn't limited to types. It can also reflect a namespace, a function, a
// specific member, and more. This tutorial mostly needs "reflect a type" and
// "reflect a member," both shown here just to establish that both work with
// the same ^^ operator; later sections do more with each.
int free_function() { return 1; }

TEST(ReflectionMetaInfo, OtherEntityKinds) {
  constexpr std::meta::info reflected_namespace = ^^std;
  constexpr std::meta::info reflected_function = ^^free_function;
  constexpr std::meta::info reflected_member = ^^Point::x;

  // Each ^^ result carries its own entity kind, queryable with a predicate
  // like std::meta::is_namespace. What you can do with each (splice it as a
  // type, call it, read its name) depends on this kind, covered starting in
  // section 2.
  static_assert(std::meta::is_namespace(reflected_namespace));
  static_assert(std::meta::is_function(reflected_function));
  static_assert(std::meta::is_nonstatic_data_member(reflected_member));
}

}  // namespace section_1

// ---------------------------------------------------------------------------
// 2. Splicing an info back into a type.
//
// Section 1 lifted a type into an opaque std::meta::info with ^^. This
// section lowers it back: typename [:some_info:] names the type that info
// reflects, wherever a type is expected: a variable declaration, a
// template argument, anywhere. typename is required because [:some_info:]
// alone is an expression (the form section 3 uses to splice a value);
// nothing about info's own type says whether it reflects a type or a
// value, so the parser needs typename to pick the type-naming parse. This
// round trip (^^ then [: :]) is called "splicing," and it's how reflection
// turns a compile-time value describing a type back into an ordinary,
// usable type.
// ---------------------------------------------------------------------------
namespace section_2 {

struct Point {
  int x = 0;
  int y = 0;
};

TEST(ReflectionSpliceType, RoundTripsToOriginal) {
  constexpr std::meta::info reflection_of_int = ^^int;

  // typename [:reflection_of_int:] names int again, so this line declares an
  // ordinary int variable, just spelled through a splice.
  typename[:reflection_of_int:] x = 5;
  EXPECT_EQ(x, 5);

  // Splicing a struct's reflection produces the exact same type, not merely
  // a look-alike: std::is_same_v confirms it.
  static_assert(std::is_same_v<typename[:^^Point:], Point>);
}

// A spliced type is usable everywhere an ordinary type name would be,
// including as a template argument.
template <typename T>
consteval bool is_class_type() {
  return std::is_class_v<T>;
}

TEST(ReflectionSpliceType, WorksAsTemplateArgument) {
  // Storing each info before splicing it, rather than splicing ^^Point
  // itself, stands in for the ordinary case: the info comes from somewhere
  // else (a parameter, a search result, ...) and only gets spliced at its
  // point of use.
  constexpr std::meta::info point_info = ^^Point;
  constexpr std::meta::info int_info = ^^int;

  static_assert(is_class_type<typename[:point_info:]>());
  static_assert(!is_class_type<typename[:int_info:]>());
}

}  // namespace section_2

// ---------------------------------------------------------------------------
// 3. Splicing an info as a call target.
//
// find_member, below, searches a type's members at compile time and
// returns the one it finds, as a std::meta::info identifying that member.
//
// A splice can stand in for a member's name in ordinary member access:
// greeter.[:member_info:] accesses whichever member member_info reflects,
// exactly as writing that member's name after the dot would. Appending
// (args) calls it, if that member is a member function; used on its own,
// with no call, it reads or writes it, if that member is a data member.
// ---------------------------------------------------------------------------
namespace section_3 {

struct Greeter {
  std::string greet(std::string_view name) const {
    return "Hello, " + std::string(name);
  }

  int value = 0;
};

consteval std::meta::info find_member(std::meta::info type,
                                      std::string_view name) {
  for (std::meta::info member :
       std::meta::members_of(type, std::meta::access_context::current())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      return member;
    }
  }
  return std::meta::info{};
}

TEST(ReflectionSpliceCall, MemberFunctionAsTarget) {
  Greeter greeter;
  constexpr std::meta::info greet_member = find_member(^^Greeter, "greet");

  // greeter.[:greet_member:](...) calls greet(), found by reflection,
  // not by writing its name at the call site.
  EXPECT_EQ(greeter.[:greet_member:]("Reader"), "Hello, Reader");
  EXPECT_EQ(greeter.[:greet_member:]("Reader"), greeter.greet("Reader"));
}

TEST(ReflectionSpliceCall, DataMemberAccess) {
  Greeter greeter;
  constexpr std::meta::info value_member = find_member(^^Greeter, "value");

  greeter.[:value_member:] = 42;
  EXPECT_EQ(greeter.value, 42);
  EXPECT_EQ(greeter.[:value_member:], 42);
}

}  // namespace section_3

// ---------------------------------------------------------------------------
// 4. Enumerating a type's members.
//
// std::meta::members_of and std::meta::nonstatic_data_members_of return
// every member of a type as a std::vector<std::meta::info>, computed inside
// a consteval function. That vector only exists during constant evaluation,
// so std::define_static_array copies it into static storage as an ordinary
// constexpr array, usable anywhere, not just inside the consteval function
// that computed it. The enumeration function below (data_members_of) does
// the walk; the constexpr variables below it (point_data_members,
// widget_member_functions) are where that copy happens.
// ---------------------------------------------------------------------------
namespace section_4 {

struct Point {
  int x = 0;
  int y = 0;
  int z = 0;
};

consteval std::vector<std::meta::info> data_members_of(std::meta::info type) {
  return std::meta::nonstatic_data_members_of(
      type, std::meta::access_context::current());
}

// define_static_array is what makes the enumerated members usable as a
// constexpr array outside the consteval function that found them.
constexpr auto point_data_members =
    std::define_static_array(data_members_of(^^Point));

TEST(ReflectionEnumerate, MembersInDeclarationOrder) {
  static_assert(point_data_members.size() == 3);
  static_assert(std::meta::identifier_of(point_data_members[0]) == "x");
  static_assert(std::meta::identifier_of(point_data_members[1]) == "y");
  static_assert(std::meta::identifier_of(point_data_members[2]) == "z");
}

struct Widget {
  int value() const { return 1; }

  void set_value(int) {}

  int payload = 0;
};

consteval std::vector<std::meta::info> member_functions_of(
    std::meta::info type) {
  // is_function alone would also match Widget's implicit special members
  // (default constructor, destructor, ...); is_special_member_function
  // excludes those, leaving just the two ordinary methods below.
  const auto is_member_function = [](std::meta::info member) {
    return std::meta::is_function(member) &&
           !std::meta::is_special_member_function(member);
  };
  return std::meta::members_of(type, std::meta::access_context::current()) |
         std::ranges::views::filter(is_member_function) |
         std::ranges::to<std::vector<std::meta::info>>();
}

constexpr auto widget_member_functions =
    std::define_static_array(member_functions_of(^^Widget));

TEST(ReflectionEnumerate, FilteredToFunctionsOnly) {
  // payload (a data member) is excluded; value() and set_value(int) (member
  // functions) are the only two results.
  static_assert(widget_member_functions.size() == 2);
  static_assert(std::meta::identifier_of(widget_member_functions[0]) ==
                "value");
  static_assert(std::meta::identifier_of(widget_member_functions[1]) ==
                "set_value");
}

}  // namespace section_4

// ---------------------------------------------------------------------------
// 5. Writing your own consteval helpers about a member.
//
// The standard library's reflection queries (std::meta::is_const,
// std::meta::parameters_of, std::meta::return_type_of, ...) are small,
// single-fact primitives. Answering richer questions means building
// consteval helpers on top of them. This section writes
// two such helpers: one classifying constness, and one building a
// signature string that distinguishes a member from other overloads of the
// same name.
// ---------------------------------------------------------------------------
namespace section_5 {

struct Widget {
  int read() const { return 1; }

  void write(int) {}

  void write(double) {}
};

// Unlike section 4's member_functions_of, this doesn't also filter out
// special member functions. The name check below already excludes them,
// since none of Widget's constructors, destructor, or assignment operators
// is named "read" or "write".
consteval std::vector<std::meta::info> members_named(std::meta::info type,
                                                     std::string_view name) {
  std::vector<std::meta::info> result;
  for (std::meta::info member :
       std::meta::members_of(type, std::meta::access_context::current())) {
    if (std::meta::is_function(member) && std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      result.push_back(member);
    }
  }
  return result;
}

TEST(ReflectionHelpers, ClassifiesConstCorrectly) {
  constexpr auto read_candidates =
      std::define_static_array(members_named(^^Widget, "read"));
  constexpr auto write_candidates =
      std::define_static_array(members_named(^^Widget, "write"));

  static_assert(read_candidates.size() == 1);
  static_assert(std::meta::is_const(read_candidates[0]));

  static_assert(write_candidates.size() == 2);
  static_assert(!std::meta::is_const(write_candidates[0]));
  static_assert(!std::meta::is_const(write_candidates[1]));
}

// A simplified signature-string builder: name plus each parameter type's
// display string, joined together, turning a member's signature into a
// distinguishable string. A real version would go on to escape that string
// into a valid C++ identifier, useful anywhere generated code needs a
// unique name per overload.
//
// A parameter's type isn't a named declaration, so std::meta::identifier_of
// (which reads a declaration's own name) doesn't apply to it.
// std::meta::display_string_of is the primitive for turning a type into a
// readable string.
consteval std::string simple_signature_string(std::meta::info member) {
  std::string result(std::meta::identifier_of(member));
  result += "(";
  bool first = true;
  for (std::meta::info parameter : std::meta::parameters_of(member)) {
    if (!first) result += ",";
    first = false;
    result += std::meta::display_string_of(
        std::meta::dealias(std::meta::type_of(parameter)));
  }
  result += ")";
  return result;
}

TEST(ReflectionHelpers, SignatureDistinguishesOverloads) {
  constexpr auto write_candidates =
      std::define_static_array(members_named(^^Widget, "write"));

  // define_static_array returns a span sized to exactly the string's own
  // length, with no guarantee of a trailing null byte, so wrap it in a
  // string_view (which carries its own length) rather than treating .data()
  // as a C-string, so comparisons never read past the span's actual bounds.
  constexpr auto int_overload_signature =
      std::define_static_array(simple_signature_string(write_candidates[0]));
  constexpr auto double_overload_signature =
      std::define_static_array(simple_signature_string(write_candidates[1]));
  std::string_view int_signature(int_overload_signature.data(),
                                 int_overload_signature.size());
  std::string_view double_signature(double_overload_signature.data(),
                                    double_overload_signature.size());

  EXPECT_EQ(int_signature, "write(int)");
  EXPECT_EQ(double_signature, "write(double)");

  // Different overloads of the same name produce different strings, which
  // is what makes a string like this usable as part of a unique, generated
  // identifier.
  EXPECT_NE(int_signature, double_signature);
}

}  // namespace section_5

// ---------------------------------------------------------------------------
// 6. Synthesizing a type's data members with define_aggregate.
//
// Every section so far has queried an existing type; this one builds one:
// std::meta::define_aggregate takes a type that's only been forward-declared
// so far (no members, incomplete) plus a list of std::meta::data_member_spec
// values, and gives that type exactly those data members, decided at
// compile time, not written out as a struct definition in source. This is
// useful anywhere generated code needs a struct whose members (their count,
// types, and names) aren't known until compile time, such as a vtable of
// function pointers with one entry per member some other type happens to
// declare.
//
// define_aggregate is called from inside a `consteval { ... }` block: a
// statement, not a function, that always runs during translation regardless
// of where it appears, even directly inside an ordinary, non-constexpr
// function body, as the first test below does.
// ---------------------------------------------------------------------------
namespace section_6 {

// Shared by both tests below, so each only has to spell out the one line
// that actually differs: the consteval block calling define_aggregate.
consteval std::vector<std::meta::info> count_ratio_specs() {
  std::vector<std::meta::info> specs;
  specs.push_back(std::meta::data_member_spec(^^int, {
                                                         .name = "count"}));
  specs.push_back(std::meta::data_member_spec(^^double, {
                                                            .name = "ratio"}));
  return specs;
}

TEST(ReflectionSynthesize, SynthesizedMembersEnumerable) {
  struct Incomplete;
  consteval { std::meta::define_aggregate(^^Incomplete, count_ratio_specs()); }

  // The consteval block above already ran during translation, so by the
  // time this ordinary, runtime TEST body executes, Incomplete already has
  // its two members. Enumerating it works exactly like enumerating an
  // ordinary, hand-written type (section 4). define_aggregate produces an
  // ordinary type, usable the same way as a hand-written one.
  constexpr auto members =
      std::define_static_array(std::meta::nonstatic_data_members_of(
          ^^Incomplete, std::meta::access_context::current()));
  static_assert(members.size() == 2);
  static_assert(std::meta::identifier_of(members[0]) == "count");
  static_assert(std::meta::identifier_of(members[1]) == "ratio");
}

// Unlike the test above, this one constructs an instance of the synthesized
// type and reads its members back, not just enumerates them. constexpr on
// instance forces that construction and read to happen in a constant
// expression, confirming a synthesized type isn't only usable at runtime.
TEST(ReflectionSynthesize, MembersFromSpecList) {
  struct Incomplete;
  consteval { std::meta::define_aggregate(^^Incomplete, count_ratio_specs()); }
  constexpr Incomplete instance{.count = 3, .ratio = 1.5};
  static_assert(instance.count == 3 && instance.ratio == 1.5);
}

}  // namespace section_6

// ---------------------------------------------------------------------------
// 7. Enum reflection: enum_to_string with template for.
//
// std::meta::enumerators_of(type) returns every enumerator of an enum
// type, in declaration order. This is the same shape of query section 4
// used for a struct's data members, applied to an enum instead. Splicing an
// enumerator's info ([:e:]) gives back the enum value itself, so a
// to_string function can compare a runtime value against it directly.
//
// `template for` is a compile-time loop: for (constexpr auto e : range)
// unrolls into ordinary code, once per element, entirely during
// compilation. This is unlike every for loop earlier in this file, which either
// ran inside a consteval function (computing a vector<info> for
// define_static_array to later hand out) or walked an already-computed
// span at runtime. Here the loop body itself becomes runtime code once per
// enumerator.
//
// enum_to_string below is P2996's own example (Reflection for C++26,
// "Enum to String"), with one adaptation: enumerators_of returns a
// std::vector<std::meta::info>, and only a copy in static storage is
// usable as template for's range. This is the same reason section 4 needed
// define_static_array.
// ---------------------------------------------------------------------------
namespace section_7 {

enum class Color { Red, Green, Blue };

template <typename E>
  requires std::is_enum_v<E>
std::string enum_to_string(E value) {
  template for (constexpr std::meta::info e :
                std::define_static_array(std::meta::enumerators_of(^^E))) {
    if (value == [:e:]) return std::string(std::meta::identifier_of(e));
  }
  return "<unnamed>";
}

TEST(ReflectionEnumToString, NamesEachEnumerator) {
  EXPECT_EQ(enum_to_string(Color::Red), "Red");
  EXPECT_EQ(enum_to_string(Color::Green), "Green");
  EXPECT_EQ(enum_to_string(Color::Blue), "Blue");
}

TEST(ReflectionEnumToString, UnmatchedValueFallsThrough) {
  // A value with no matching enumerator falls through every generated
  // comparison to the fallback return, reached here via an explicit
  // cast, since Color itself declares no such value.
  EXPECT_EQ(enum_to_string(static_cast<Color>(99)), "<unnamed>");
}

}  // namespace section_7

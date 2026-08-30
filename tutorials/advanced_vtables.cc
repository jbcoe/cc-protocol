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

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "consteval_check.h"

// A study of using vtables for overloaded member functions and `operator()`.
// This tutorial follows on from the work in `tutorials/polymorphism.cc` and
// `tutorials/reflection.cc`

namespace xyz::tutorials::simple_vtable {

class Cat {
 public:
  std::string_view noise() const { return "Meow"; }
};

struct vtable {
  std::string_view (*noise_func_)(const void* data);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    // Making the vtable `constexpr` avoids runtime cost (synchronization
    // between threads) when accessing static variables.
    constexpr static vtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return vtable_->noise_func_(data_); }
};

TEST(TutorialsVtables, SimpleVtable) {
  Cat cat;

  AnimalPtr a(&cat);

  EXPECT_EQ(a.noise(), "Meow");
}

}  // namespace xyz::tutorials::simple_vtable

namespace xyz::tutorials::vtable_with_overload {
class Cat {
 public:
  std::string_view noise() const { return "Meow"; }

  std::string_view noise(int) const { return "Purr"; }
};

struct vtable {
  // With overloads, vtable entry names must represent type signatures.
  std::string_view (*noise_void_func_)(const void* data);
  std::string_view (*noise_int_func_)(const void* data, int);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    // Making the vtable `constexpr` avoids runtime cost (synchronization
    // between threads) when accessing static variables.
    constexpr static vtable vtable_for_type = {
        .noise_void_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
        .noise_int_func_ =
            +[](const void* data, int x) {
              return static_cast<const T*>(data)->noise(x);
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const {  //
    return vtable_->noise_void_func_(data_);
  }

  std::string_view noise(int x) const {
    return vtable_->noise_int_func_(data_, x);
  }
};

TEST(TutorialsVtables, VtableWithOverloads) {
  Cat cat;

  AnimalPtr a(&cat);

  EXPECT_EQ(a.noise(), "Meow");
  EXPECT_EQ(a.noise(0), "Purr");
}
}  // namespace xyz::tutorials::vtable_with_overload

namespace xyz::tutorials::vtable_with_operators {
struct Cat {
  std::string_view operator()() const { return "Meow"; }
};

struct vtable {
  // `operator()` is an invalid identifier so we must use a mangled name.
  std::string_view (*operator_call_fn)(const void* data);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    // Making the vtable `constexpr` avoids runtime cost (synchronization
    // between threads) when accessing static variables.
    constexpr static vtable vtable_for_type = {
        .operator_call_fn =
            +[](const void* data) {
              return static_cast<const T*>(data)->operator()();
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view operator()() const {  //
    return vtable_->operator_call_fn(data_);
  }
};

TEST(TutorialsVtables, VtableWithCallOperator) {
  Cat cat;

  AnimalPtr a(&cat);

  EXPECT_EQ(a(), "Meow");
}
}  // namespace xyz::tutorials::vtable_with_operators

namespace xyz::tutorials::name_mangling_for_vtable {

using std::meta::dealias;
using std::meta::is_const;
using std::meta::is_const_type;
using std::meta::is_function;
using std::meta::is_fundamental_type;
using std::meta::is_lvalue_reference_type;
using std::meta::is_operator_function;
using std::meta::is_pointer_type;
using std::meta::is_rvalue_reference_type;
using std::meta::is_special_member_function;
using std::meta::is_static_member;
using std::meta::parameters_of;
using std::meta::remove_const;
using std::meta::remove_pointer;
using std::meta::remove_reference;
using std::meta::type_of;
using std::views::filter;

struct Cat {
  std::string_view operator()() const;
  std::string_view operator()();
  std::string_view operator()(int) const;
  std::string_view operator()(int);

  std::string_view operator[](int) const;

  std::string_view operator->() const;

  std::string_view noise() const;
  std::string_view noise(int) const;
  std::string_view noise(int*) const;
  std::string_view noise(int&) const;
  std::string_view noise(const int&) const;
  std::string_view noise(std::size_t) const;
  std::string_view noise(std::string_view) const;
};

constexpr bool is_identifier_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

// Replaces every non-identifier character with `_`.
consteval std::string sanitize(std::string_view text) {
  std::string out;
  for (char c : text) {
    out += is_identifier_char(c) ? c : '_';
  }
  return out;
}

consteval std::string decimal(std::size_t value) {
  std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> buffer;
  auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), result.ptr);
}

// Length-prefixed with no separator, matching the Itanium ABI's own
// <source-name> production (`Foo` mangles as `3Foo`, not `3_Foo`). Stays
// unambiguous because every atom is either a genuine C++ identifier, which
// never starts with a digit, or one of this file's own non-digit tags, so
// a decoder always knows where the length ends and the payload begins.
consteval std::string mangle_atom(std::string_view text) {
  return decimal(text.size()) + std::string(text);
}

// Length-prefixes each `::`-separated component of a qualified name
// individually, so e.g. `A::B` can't collide with a hypothetical `A_B`.
consteval std::string mangle_qualified_name(std::string_view name) {
  std::string out;
  std::size_t pos = 0;
  while (true) {
    std::size_t next = name.find("::", pos);
    out += mangle_atom(sanitize(name.substr(pos, next - pos)));
    if (next == std::string_view::npos) break;
    pos = next + 2;  // Advance past "::".
  }
  return out;
}

// Itanium ABI <builtin-type> codes for fundamental types.
consteval std::optional<std::string_view> builtin_type_code(
    std::meta::info type) {
  type = dealias(type);
  if (type == ^^int) return "i";
  if (type == dealias(^^std::size_t)) return "m";
  if (is_fundamental_type(type)) {
    // Limited type handling for brevity.
    throw std::runtime_error(
        "builtin_type_code: no mnemonic for this fundamental type");
  }
  return std::nullopt;
}

// Recurses through pointer/reference/const layers, tagging each with a
// single (Itanium ABI) letter, then descends to the pointee/referee.
consteval std::string mangle_type(std::meta::info type) {
  type = dealias(type);
  if (is_const_type(type)) return "K" + mangle_type(remove_const(type));
  if (is_pointer_type(type)) return "P" + mangle_type(remove_pointer(type));
  if (is_lvalue_reference_type(type)) {
    return "R" + mangle_type(remove_reference(type));
  }
  if (is_rvalue_reference_type(type)) {
    return "O" + mangle_type(remove_reference(type));
  }
  if (auto builtin = builtin_type_code(type)) return std::string(*builtin);
  return mangle_qualified_name(std::meta::display_string_of(type));
}

// Itanium ABI <operator-name> mnemonics for operators.
consteval std::string operator_code(std::meta::operators op) {
  using enum std::meta::operators;
  switch (op) {
    case op_parentheses:
      return "cl";
    case op_square_brackets:
      return "ix";
    case op_arrow:
      return "pt";
    default:
      // Limited operator handling for brevity.
      throw std::runtime_error("operator_code: no mnemonic for this operator");
  }
}

// `identifier_of` throws for `operator()`, which has no identifier; use
// its Itanium mnemonic instead.
consteval std::string base_name_of(std::meta::info fn) {
  if (is_operator_function(fn)) {
    return operator_code(std::meta::operator_of(fn));
  }
  return std::string(identifier_of(fn));
}

// Distinguishes overloads by folding each parameter's type and the
// function's const-qualification into the name.
consteval std::string_view mangle(std::meta::info fn) {
  std::string name = "fn_" + mangle_atom(base_name_of(fn));
  auto params = parameters_of(fn);
  for (auto param : params) {
    name += mangle_type(type_of(param));
  }
  if (is_const(fn)) name += "K";
  return std::define_static_string(name);
}

// `Type`'s public, non-special member functions, in declaration order.
template <std::meta::info Type>
constexpr auto member_functions_of = std::define_static_array(
    members_of(Type, std::meta::access_context::unprivileged()) |
    filter(is_function) | filter(std::not_fn(is_static_member)) |
    filter(std::not_fn(is_special_member_function)));

TEST(TutorialsVtables, NameMangling) {
  constexpr auto m_fns = member_functions_of<^^Cat>;

  // operator()() const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[0]) == "fn_2clK");
  // operator()()
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[1]) == "fn_2cl");
  // operator()(int) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[2]) == "fn_2cliK");
  // operator()(int)
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[3]) == "fn_2cli");
  // operator[](int) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[4]) == "fn_2ixiK");
  // operator->() const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[5]) == "fn_2ptK");
  // noise() const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[6]) == "fn_5noiseK");
  // noise(int) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[7]) == "fn_5noiseiK");
  // noise(int*) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[8]) == "fn_5noisePiK");
  // noise(int&) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[9]) == "fn_5noiseRiK");
  // noise(const int&) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[10]) == "fn_5noiseRKiK");
  // noise(std::size_t) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[11]) == "fn_5noisemK");
  // noise(std::string_view) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[12]) ==
                      "fn_5noise3std23basic_string_view_char_K");
}

}  // namespace xyz::tutorials::name_mangling_for_vtable

// We can use the mangled names when writing the vtable to allow functions to be
// disambiguated.

namespace xyz::tutorials::name_mangled_vtable {

using std::meta::dealias;
using std::meta::parameters_of;
using std::meta::type_of;
using xyz::tutorials::name_mangling_for_vtable::mangle;
using xyz::tutorials::name_mangling_for_vtable::member_functions_of;

struct Cat {
  std::string_view noise() const { return "Meow"; }

  std::string_view noise(int) const { return "Purr"; }

  std::string_view operator()() const { return "Meow"; }

  std::string_view operator()(int) const { return "Purr"; }
};

template <typename R, typename... Args>
using const_fn_ptr_t = R (*)(const void*, Args...);

template <std::meta::info Interface>
consteval std::vector<std::meta::info> mangled_vtable_specs() {
  std::vector<std::meta::info> specs;
  template for (constexpr std::meta::info fn : member_functions_of<Interface>) {
    std::vector<std::meta::info> fn_args{dealias(return_type_of(fn))};
    for (std::meta::info param : parameters_of(fn)) {
      fn_args.push_back(dealias(type_of(param)));
    }
    std::meta::info fn_ptr_type = substitute(^^const_fn_ptr_t, fn_args);
    specs.push_back(data_member_spec(fn_ptr_type, {.name = mangle(fn)}));
  }
  return specs;
}

template <typename T>
struct vtable_generator {
  struct vtable;
  consteval { define_aggregate(^^vtable, mangled_vtable_specs<^^T>()); }
};

class AnimalPtr {
  using vtable_t = vtable_generator<Cat>::vtable;
  const void* data_;
  const vtable_t* vtable_;

 public:
  template <typename T>
  explicit AnimalPtr(const T* animal) : data_(animal) {
    constexpr static vtable_t vtable_for_type{
        .fn_5noiseK =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
        .fn_5noiseiK =
            +[](const void* data, int x) {
              return static_cast<const T*>(data)->noise(x);
            },
        .fn_2clK =
            +[](const void* data) {  //
              return (*static_cast<const T*>(data))();
            },
        .fn_2cliK = +[](const void* data,
                        int x) {  //
          return (*static_cast<const T*>(data))(x);
        }};
    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return vtable_->fn_5noiseK(data_); }

  std::string_view noise(int x) const { return vtable_->fn_5noiseiK(data_, x); }

  std::string_view operator()() const { return vtable_->fn_2clK(data_); }

  std::string_view operator()(int x) const {
    return vtable_->fn_2cliK(data_, x);
  }
};

TEST(TutorialsVtables, NameMangledVtable) {
  Cat cat;
  AnimalPtr p(&cat);

  EXPECT_EQ(p.noise(), "Meow");
  EXPECT_EQ(p.noise(3), "Purr");
  EXPECT_EQ(p(), "Meow");
  EXPECT_EQ(p(3), "Purr");
}

}  // namespace xyz::tutorials::name_mangled_vtable

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
#ifndef XYZ_REFLECTION_NAME_MANGLING_H_
#define XYZ_REFLECTION_NAME_MANGLING_H_

// Names a member function with a string that's a valid C++ identifier: only
// letters, digits and underscore, none of the characters (`:`, `(`, `)`,
// `<`, `>`) that a plain rendering of a signature would contain. The
// encoding reuses the Itanium C++ ABI's member-mangling scheme, which
// already solves this problem for linker symbol names, rather than
// inventing a new one.

#include <array>
#include <charconv>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace xyz::name_mangling {

namespace detail {

template <typename T>
consteval std::string decimal(T value) {
  std::array<char, std::numeric_limits<T>::digits10 + 1> buffer;
  auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), result.ptr);
}

// Length-prefixed with no separator, as for an Itanium <source-name>.
consteval std::string mangle_atom(std::string_view text) {
  return decimal(text.size()) + std::string(text);
}

// Forward-declared: mutually recursive with `mangle_scope_component` via a
// template argument that is itself a class-template specialisation.
consteval std::string mangle_type(std::meta::info type);

template <typename T>
consteval std::string signed_decimal(std::meta::info argument) {
  T value = extract<T>(argument);
  using Unsigned = std::make_unsigned_t<T>;
  if constexpr (std::is_signed_v<T>) {
    if (value < 0) {
      return "n" +
             decimal(static_cast<Unsigned>(0) - static_cast<Unsigned>(value));
    }
  }
  return decimal(static_cast<Unsigned>(value));
}

// The decimal value of `argument` if its type is one of `Integral...`.
template <typename... Integral>
consteval std::optional<std::string> integral_decimal(
    std::meta::info argument) {
  std::meta::info type = dealias(type_of(argument));
  std::optional<std::string> result;
  // The fold's own value is discarded; the match is recorded through
  // `result`. The cast keeps Clang's -Wunused-value quiet.
  (void)((type == ^^Integral
              ? (result = signed_decimal<Integral>(argument), true)
              : false) ||
         ...);
  return result;
}

// A non-type template argument as an Itanium <expr-primary>, `L<type><value>E`.
// Enumeration values are written by enumerator name rather than numeric
// value, which needs no knowledge of the underlying type.
consteval std::string mangle_template_argument_value(std::meta::info argument) {
  std::meta::info type = dealias(type_of(argument));
  std::string out = "L" + mangle_type(type);
  if (type == ^^bool) {
    return out + (extract<bool>(argument) ? "1" : "0") + "E";
  }
  if (std::optional<std::string> value =
          integral_decimal<char, signed char, unsigned char, wchar_t, char8_t,
                           char16_t, char32_t, short, unsigned short, int,
                           unsigned int, long, unsigned long, long long,
                           unsigned long long>(argument)) {
    return out + *value + "E";
  }
  if (is_enum_type(type)) {
    std::vector<std::meta::info> enumerators = enumerators_of(type);
    for (std::meta::info enumerator : enumerators) {
      if (constant_of(enumerator) == argument) {
        return out + mangle_atom(identifier_of(enumerator)) + "E";
      }
    }
  }
  throw std::runtime_error(
      "name mangling: unsupported non-type template argument");
}

// Mangles one scope level (a namespace, class, enumeration or function),
// folding in template arguments when `scope` is a class-template
// specialisation.
consteval std::string mangle_scope_component(std::meta::info scope) {
  if (is_namespace(scope) && !has_identifier(scope)) return "12_GLOBAL__N_1";
  std::meta::info named =
      has_template_arguments(scope) ? template_of(scope) : scope;
  if (!has_identifier(named)) {
    throw std::runtime_error("name mangling: unnamed types are not supported");
  }
  std::string out = mangle_atom(identifier_of(named));
  if (has_template_arguments(scope)) {
    out += "I";
    std::vector<std::meta::info> arguments = template_arguments_of(scope);
    for (std::meta::info argument : arguments) {
      if (is_type(argument)) {
        out += mangle_type(argument);
      } else if (is_value(argument)) {
        out += mangle_template_argument_value(argument);
      } else {
        throw std::runtime_error(
            "name mangling: unsupported template argument");
      }
    }
    out += "E";
  }
  return out;
}

// Walks `parent_of` up to (not including) the global namespace, then wraps
// 2+ levels in Itanium's N...E delimiters so a qualified name can't be
// confused with a sequence of unqualified ones.
consteval std::string mangle_qualified_name(std::meta::info type) {
  std::vector<std::string> components;
  for (std::meta::info scope = type; scope != ^^::;
       scope = dealias(parent_of(scope))) {
    components.push_back(mangle_scope_component(scope));
  }
  std::string joined;
  for (const std::string& component : components | std::views::reverse) {
    joined += component;
  }
  return components.size() > 1 ? "N" + joined + "E" : joined;
}

// Itanium ABI <builtin-type> codes for fundamental types.
consteval std::optional<std::string_view> builtin_type_code(
    std::meta::info type) {
  constexpr std::array<std::pair<std::meta::info, std::string_view>, 23> codes{
      {{^^void, "v"},
       {^^bool, "b"},
       {^^char, "c"},
       {^^signed char, "a"},
       {^^unsigned char, "h"},
       {^^short, "s"},
       {^^unsigned short, "t"},
       {^^int, "i"},
       {^^unsigned int, "j"},
       {^^long, "l"},
       {^^unsigned long, "m"},
       {^^long long, "x"},
       {^^unsigned long long, "y"},
       {^^__int128, "n"},
       {^^unsigned __int128, "o"},
       {^^float, "f"},
       {^^double, "d"},
       {^^long double, "e"},
       {^^wchar_t, "w"},
       {^^char8_t, "Du"},
       {^^char16_t, "Ds"},
       {^^char32_t, "Di"},
       {^^std::nullptr_t, "Dn"}}};
  for (auto [builtin, code] : codes) {
    if (dealias(builtin) == type) return code;
  }
  if (is_fundamental_type(type)) {
    throw std::runtime_error(
        "name mangling: no mnemonic for this fundamental type");
  }
  return std::nullopt;
}

// Recurses through cv/pointer/reference/array/function layers, tagging each
// with its Itanium prefix, then descends to the pointee/referee/element.
consteval std::string mangle_type(std::meta::info type) {
  type = dealias(type);
  if (is_const_type(type) || is_volatile_type(type)) {
    std::string qualifiers = is_volatile_type(type) ? "V" : "";
    if (is_const_type(type)) qualifiers += "K";
    return qualifiers + mangle_type(remove_cv(type));
  }
  if (is_pointer_type(type)) return "P" + mangle_type(remove_pointer(type));
  if (is_lvalue_reference_type(type)) {
    return "R" + mangle_type(remove_reference(type));
  }
  if (is_rvalue_reference_type(type)) {
    return "O" + mangle_type(remove_reference(type));
  }
  if (is_array_type(type)) {
    std::string out = "A";
    if (is_bounded_array_type(type)) out += decimal(extent(type));
    return out + "_" + mangle_type(remove_extent(type));
  }
  if (is_function_type(type)) {
    std::string out = is_noexcept(type) ? "DoF" : "F";
    out += mangle_type(return_type_of(type));
    std::vector<std::meta::info> parameters = parameters_of(type);
    if (parameters.empty()) out += "v";
    for (std::meta::info parameter : parameters) {
      out += mangle_type(is_type(parameter) ? parameter : type_of(parameter));
    }
    return out + "E";
  }
  if (auto code = builtin_type_code(type)) return std::string(*code);
  if (is_class_type(type) || is_union_type(type) || is_enum_type(type)) {
    return mangle_qualified_name(type);
  }
  throw std::runtime_error("name mangling: unsupported parameter type");
}

// Returns the mangled function-name atom for `function`: the Itanium
// <operator-name> `cl` for the call operator, or a length-prefixed
// <source-name> for its identifier otherwise. `identifier_of` throws for
// `operator()`, which has no identifier.
consteval std::string base_name_of(std::meta::info function) {
  if (is_operator_function(function) &&
      operator_of(function) == std::meta::operators::op_parentheses) {
    return "cl";
  }
  return mangle_atom(identifier_of(function));
}

}  // namespace detail

// Names the member function `function`, distinguishing overloads by folding
// its cv- and ref-qualification, noexcept-ness, return type and each
// parameter's type into the name. Matches the Itanium ABI's own placement
// for the qualifiers: they sit inside `N...E`, wrapping the function name,
// ahead of the return type and parameter list.
//
// C++ overload resolution never needs the return type or noexcept-ness to
// disambiguate the member functions of one interface, so folding them in is
// redundant there. It matters once a vtable entry is found by this name
// across two different interfaces: two unrelated interfaces can each
// declare `get() const` with a different return type or noexcept-ness, and
// without this, both would mangle to the same entry name.
consteval std::string mangle(std::meta::info function) {
  std::string qualifiers;
  if (is_volatile(function)) qualifiers += "V";
  if (is_const(function)) qualifiers += "K";
  if (is_lvalue_reference_qualified(function)) qualifiers += "R";
  if (is_rvalue_reference_qualified(function)) qualifiers += "O";
  std::string atom = detail::base_name_of(function);
  std::string name =
      "fn_" + (qualifiers.empty() ? atom : "N" + qualifiers + atom + "E");
  name += is_noexcept(function) ? "Do" : "";
  name += detail::mangle_type(return_type_of(function));
  std::vector<std::meta::info> parameters = parameters_of(function);
  for (std::meta::info parameter : parameters) {
    name += detail::mangle_type(type_of(parameter));
  }
  return name;
}

}  // namespace xyz::name_mangling

#endif  // XYZ_REFLECTION_NAME_MANGLING_H_

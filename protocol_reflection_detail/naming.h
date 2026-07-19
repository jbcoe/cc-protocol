/* Copyright (c) 2026 The XYZ Protocol Authors. All Rights Reserved.

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
// Deterministic, stable vtable entry naming for the C++26 reflection backend.
// Split out of protocol_reflection.h because this piece is self-contained
// (every function here depends only on the standard library and <meta>, none
// on the rest of the backend's member-enumeration or vtable machinery) and
// independently testable: naming_test.cc includes this header directly
// rather than the whole backend.
#ifndef XYZ_PROTOCOL_REFLECTION_NAMING_H_
#define XYZ_PROTOCOL_REFLECTION_NAMING_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <meta>
#include <string>
#include <string_view>

namespace xyz {
namespace reflection_detail {

using std::meta::info;

// ---------------------------------------------------------------------------
// Deterministic, stable entry naming
//
// Vtable entry names must be valid C++ identifiers, unique within one
// interface's vtable, and a deterministic function of a member's own
// signature, not of its position among other members. The last part
// matters because narrowing (see protocol_reflection.h's "Vtable narrowing
// maps") matches vtable entries between two *different* interfaces by name,
// so the same signature must always produce the same name regardless of
// what other members its interface happens to declare.
//
// Rather than hashing the signature (which only gives a probabilistic
// guarantee against collisions), identifier_safe_string encodes it exactly:
// every byte outside [a-zA-Z0-9], including a literal `_` itself, is
// replaced by `_` followed by its two-digit hex value. This is injective
// by construction: scanning left to right, a `_` in the output always
// starts a two-hex-digit escape, since no unescaped `_` from the input
// ever survives unescaped, so two different signatures can never collide
// the way two different hash inputs theoretically could.
// ---------------------------------------------------------------------------

consteval std::string identifier_safe_string(std::string_view text) {
  const char* hex_digits = "0123456789abcdef";
  std::string result;
  for (unsigned char byte : text) {
    bool is_identifier_safe = (byte >= 'a' && byte <= 'z') ||
                              (byte >= 'A' && byte <= 'Z') ||
                              (byte >= '0' && byte <= '9');
    if (is_identifier_safe) {
      result += static_cast<char>(byte);
    } else {
      result += '_';
      result += hex_digits[(byte >> 4) & 0xF];
      result += hex_digits[byte & 0xF];
    }
  }
  return result;
}

// Identifier-safe spelling for an operator kind: every std::meta::operators
// enumerator is named op_<spelling>, found here by reflection (instead of one
// hand-written switch case per operator) and returned with that prefix
// stripped.
consteval std::string_view operator_spelling(std::meta::operators kind) {
  template for (constexpr info e : std::define_static_array(
                    std::meta::enumerators_of(^^std::meta::operators))) {
    if (kind == [:e:]) {
      return std::string_view(std::meta::identifier_of(e)).substr(3);
    }
  }
  return "";
}

consteval std::string mangled_member_name(info member) {
  if (std::meta::has_identifier(member)) {
    return std::string(std::meta::identifier_of(member));
  }
  return "operator_" +
         std::string(operator_spelling(std::meta::operator_of(member)));
}

consteval std::string member_signature_string(info member) {
  std::string signature = mangled_member_name(member);
  signature += "(";
  bool first = true;
  for (info parameter : std::meta::parameters_of(member)) {
    if (!first) signature += ",";
    first = false;
    signature += std::meta::display_string_of(
        std::meta::dealias(std::meta::type_of(parameter)));
  }
  signature += ")";
  if (std::meta::is_const(member)) signature += " const";
  if (std::meta::is_noexcept(member)) signature += " noexcept";
  return signature;
}

consteval std::string vtable_entry_name(info member) {
  return identifier_safe_string(member_signature_string(member));
}

}  // namespace reflection_detail
}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_NAMING_H_

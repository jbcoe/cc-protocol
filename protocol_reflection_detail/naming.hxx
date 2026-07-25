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
// Deterministic, collision-proof names for generated vtable/forwarder
// entries. Every byte outside [a-zA-Z0-9], including a literal underscore,
// becomes an underscore plus two lowercase hex digits. This is a bijection
// on byte sequences, so distinct inputs always produce distinct
// identifiers.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_NAMING_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_NAMING_HXX_

#include <meta>
#include <string>
#include <string_view>

namespace xyz::reflection_detail {

// `identifier_safe_string` must run at compile time.
// C++ library utilities like `std::isalnum` are not constexpr functions so we
// hand-roll our own.
//
// The 8-bit character for non-alphanumeric characters is
// split into 2 4-bit values, each of which is mapped to an alphanumeric
// character. We prefix this character pair with `_` to give each
// non-alphanumeric character a unique representation.
constexpr std::string identifier_safe_string(std::string_view s) {
  constexpr std::string_view hex_digits = "0123456789abcdef";
  std::string result;
  result.reserve(s.size());
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
      result += static_cast<char>(c);
    } else {
      result += '_';
      result += hex_digits[c >> 4];
      result += hex_digits[c & 0xf];
    }
  }
  return result;
}

// The generated vtable-entry name for a reflected member function or data
// member: `member`'s own identifier, verbatim, not escaped, since a named
// member's identifier is already a valid C++ identifier. Operators, which
// have no identifier_of, need separate handling. This is what a forwarder's
// single public-facing data member is called (forwarders.hxx); it must
// stay callable as e.g. `.compute(...)` regardless of how many overloads
// `compute` has, so every overload of the same name shares this name on
// purpose.
consteval std::string vtable_entry_name(std::meta::info member) {
  return std::string(std::meta::identifier_of(member));
}

// A generated vtable struct's entry name for `member`, unique per exact
// overload. Unlike vtable_entry_name above, this qualifies by the member's
// full display string (return type, parameter types, constness), not just
// its identifier: a vtable struct is an internal, never user-visible
// implementation detail with one data member per overload, so two
// overloads sharing a name (e.g. interface C's three `compute`s) need two
// distinct entries here, even though they share one forwarder name.
consteval std::string vtable_slot_name(std::meta::info member) {
  return identifier_safe_string(std::meta::display_string_of(member));
}

// The data member of `struct_type` named `name`, for reading or writing
// a reflection-built struct's entry by name (a vtable_slot_name or
// vtable_entry_name result) rather than position.
consteval std::meta::info find_data_member(std::meta::info struct_type,
                                           std::string_view name) {
  for (std::meta::info member : std::meta::nonstatic_data_members_of(
           struct_type, std::meta::access_context::current())) {
    if (std::meta::identifier_of(member) == name) return member;
  }
  throw std::meta::exception("data member not found", ^^void);
}

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_NAMING_HXX_

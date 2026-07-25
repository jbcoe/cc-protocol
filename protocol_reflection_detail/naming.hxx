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
// member, found by name. Operators, which have no identifier_of, need
// separate handling.
consteval std::string vtable_entry_name(std::meta::info member) {
  return identifier_safe_string(std::meta::identifier_of(member));
}

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_NAMING_HXX_

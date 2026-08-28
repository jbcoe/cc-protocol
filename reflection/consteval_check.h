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
#ifndef XYZ_REFLECTION_CONSTEVAL_CHECK_H_
#define XYZ_REFLECTION_CONSTEVAL_CHECK_H_

// A compile-time assertion helper for use inside `consteval` blocks and
// functions. `static_assert` requires its operand to be a constant expression
// on its own, which locals in a consteval block are not; a bare `throw` works
// but the diagnostic only shows the hand-written message. XYZ_CONSTEVAL_CHECK
// decomposes `lhs OP rhs`, and on failure throws consteval_check_failure whose
// what() reports "<file>:<line>: check failed: <expression> [<lhs> OP <rhs>]".
// GCC prints what() of an uncaught exception during constant evaluation.
// Everything is constexpr, so the macro also works at run time, which is how
// consteval_check_test.cc inspects the thrown message.

#include <charconv>
#include <concepts>
#include <exception>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if __has_include(<meta>)
#include <meta>
#endif

namespace xyz {

class consteval_check_failure : public std::exception {
 public:
  constexpr explicit consteval_check_failure(std::string message)
      : message_(std::move(message)) {}

  constexpr const char* what() const noexcept override {
    return message_.c_str();
  }

 private:
  std::string message_;
};

namespace detail::consteval_check {

// Best-effort rendering of an operand for the diagnostic.
template <typename T>
constexpr std::string to_display_string(const T& value) {
  if constexpr (std::is_same_v<T, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_integral_v<T>) {
    char buffer[32];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
    return "\"" + std::string(std::string_view(value)) + "\"";
#if __has_include(<meta>)
  } else if constexpr (std::is_same_v<T, std::meta::info>) {
    return std::meta::display_string_of(value);
#endif
  } else {
    return "{?}";
  }
}

struct check_result {
  bool passed;
  std::string expansion;  // e.g. "\"y\" == \"x\"", or "" for a unary check.
};

template <typename Lhs>
struct captured_value {
  Lhs value;  // Lhs may be a reference type; captured by forwarding reference.

  template <typename Rhs>
  friend constexpr check_result operator==(captured_value&& lhs, Rhs&& rhs) {
    return {static_cast<bool>(lhs.value == rhs),
            to_display_string(lhs.value) + " == " + to_display_string(rhs)};
  }

  template <typename Rhs>
  friend constexpr check_result operator!=(captured_value&& lhs, Rhs&& rhs) {
    return {static_cast<bool>(lhs.value != rhs),
            to_display_string(lhs.value) + " != " + to_display_string(rhs)};
  }

  template <typename Rhs>
  friend constexpr check_result operator<(captured_value&& lhs, Rhs&& rhs) {
    return {static_cast<bool>(lhs.value < rhs),
            to_display_string(lhs.value) + " < " + to_display_string(rhs)};
  }

  template <typename Rhs>
  friend constexpr check_result operator>(captured_value&& lhs, Rhs&& rhs) {
    return {static_cast<bool>(lhs.value > rhs),
            to_display_string(lhs.value) + " > " + to_display_string(rhs)};
  }

  template <typename Rhs>
  friend constexpr check_result operator<=(captured_value&& lhs, Rhs&& rhs) {
    return {static_cast<bool>(lhs.value <= rhs),
            to_display_string(lhs.value) + " <= " + to_display_string(rhs)};
  }

  template <typename Rhs>
  friend constexpr check_result operator>=(captured_value&& lhs, Rhs&& rhs) {
    return {static_cast<bool>(lhs.value >= rhs),
            to_display_string(lhs.value) + " >= " + to_display_string(rhs)};
  }

  // Unary use (XYZ_CONSTEVAL_CHECK(some_bool)): no comparison operator is
  // ever applied, so `report` calls this directly instead.
  constexpr check_result evaluate() const
    requires std::convertible_to<Lhs, bool>
  {
    return {static_cast<bool>(value), ""};
  }
};

struct value_capturer {
  template <typename T>
  constexpr captured_value<T&&> operator<=(T&& lhs) const {
    return captured_value<T&&>{std::forward<T>(lhs)};
  }
};

constexpr void report(check_result result, std::string_view expression_text,
                       std::source_location location) {
  if (result.passed) return;
  std::string message = std::string(location.file_name()) + ":" +
                         to_display_string(location.line()) +
                         ": check failed: " + std::string(expression_text);
  if (!result.expansion.empty()) {
    message += " [" + result.expansion + "]";
  }
  throw consteval_check_failure(std::move(message));
}

// Overload for the unary case, where no comparison operator was ever
// applied to the captured value.
template <typename Lhs>
constexpr void report(captured_value<Lhs> value,
                       std::string_view expression_text,
                       std::source_location location) {
  report(value.evaluate(), expression_text, location);
}

}  // namespace detail::consteval_check
}  // namespace xyz

#define XYZ_CONSTEVAL_CHECK(...)                                          \
  do {                                                                    \
    _Pragma("GCC diagnostic push")                                        \
        _Pragma("GCC diagnostic ignored \"-Wparentheses\"")               \
            ::xyz::detail::consteval_check::report(                       \
                (::xyz::detail::consteval_check::value_capturer{} <=      \
                 __VA_ARGS__),                                            \
                #__VA_ARGS__, std::source_location::current());           \
    _Pragma("GCC diagnostic pop")                                         \
  } while (false)

#endif  // XYZ_REFLECTION_CONSTEVAL_CHECK_H_

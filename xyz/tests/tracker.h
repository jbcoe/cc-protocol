/* Copyright (c) 2016 The Value Types Authors. All Rights Reserved.

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

#ifndef XYZ_TRACKER_H_
#define XYZ_TRACKER_H_

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include "xyz/detail/small_buffer.hh"

namespace xyz::tests {

// Counts the copies, moves and live objects of `Tracker`s.
struct Counts {
  std::size_t copies = 0;
  std::size_t moves = 0;
  std::size_t live = 0;

  Counts() = default;
  Counts(const Counts&) = delete ("Unused");
  Counts(Counts&&) = delete ("Unused");
  Counts& operator=(const Counts&) = delete ("Unused");
  Counts& operator=(Counts&&) = delete ("Unused");

  ~Counts() { EXPECT_EQ(live, 0); }
};

// Conforms to an interface whose only member function is `int value() const`.
// `PaddingSize` bytes of padding decide whether it fits in the small buffer.
template <std::size_t PaddingSize>
class Tracker {
  Counts* counts_;
  int value_;
  [[maybe_unused]] std::array<std::byte, PaddingSize> padding_{};

 public:
  Tracker(Counts* counts, int value) noexcept : counts_(counts), value_(value) {
    ++counts_->live;
  }

  Tracker(const Tracker& other) : counts_(other.counts_), value_(other.value_) {
    ++counts_->live;
    ++counts_->copies;
  }

  Tracker(Tracker&& other) noexcept
      : counts_(other.counts_), value_(other.value_) {
    ++counts_->live;
    ++counts_->moves;
  }

  Tracker& operator=(const Tracker&) = delete ("Unused");
  Tracker& operator=(Tracker&&) = delete ("Unused");

  ~Tracker() { --counts_->live; }

  int value() const { return value_; }
};

using SmallTracker = Tracker<0>;
using LargeTracker = Tracker<xyz::detail::small_buffer_size>;

}  // namespace xyz::tests

#endif  // XYZ_TRACKER_H_

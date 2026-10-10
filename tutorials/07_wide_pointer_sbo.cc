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
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access): `Storage` is a union.
// NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): `swap` and
// the tests use moved-from objects.
// NOLINTBEGIN(performance-unnecessary-copy-initialization): the tests copy to
// count copies.

// An exploration of small buffer optimization for an owning wide pointer.
//
// A wide pointer using a small buffer for storage is intentionally non-empty
// after being moved from.
namespace xyz::tutorials::wide_pointer_small_buffer {

enum class StorageKind {
  None,
  Buffer,
  Indirect,
};

union Storage {
  constexpr static std::size_t BufferSize = 3 * sizeof(void*);
  std::array<std::byte, BufferSize> buffer_;
  void* ptr_;
};

struct BufferStorageTag {};

static constexpr BufferStorageTag buffer_storage;

struct IndirectStorageTag {};

static constexpr IndirectStorageTag indirect_storage;

struct VTable {
  void (*destroy)(Storage& storage) noexcept;
  void (*copy)(const Storage& src, Storage* dest);
  void (*move)(Storage& src, Storage* dest) noexcept;
  StorageKind storage_kind;
};

class SBOWidePtr {
  const VTable* vtable_;

  Storage storage_;

  template <typename U>
  constexpr static VTable indirect_storage_vtable{
      .destroy =
          +[](Storage& storage) noexcept {
            U* object = static_cast<U*>(storage.ptr_);
            delete object;
          },
      .copy =
          +[](const Storage& src, Storage* dest) {
            const U* object = static_cast<const U*>(src.ptr_);
            dest->ptr_ = new U(*object);
          },
      .move =
          +[](Storage& src, Storage* dest) noexcept {
            dest->ptr_ = std::exchange(src.ptr_, nullptr);
          },
      .storage_kind = StorageKind::Indirect,
  };

  template <typename U>
  constexpr static VTable buffer_storage_vtable{
      .destroy =
          +[](Storage& storage) noexcept {
            U* object =
                std::launder(reinterpret_cast<U*>(storage.buffer_.data()));
            object->~U();
          },
      .copy =
          +[](const Storage& src, Storage* dest) {
            const U* object =
                std::launder(reinterpret_cast<const U*>(src.buffer_.data()));
            // Make the array the active member. `std::start_lifetime_as` would
            // avoid zero-initializing it, but libc++ does not provide it.
            std::construct_at(&dest->buffer_);
            new (dest->buffer_.data()) U(*object);
          },
      .move =
          +[](Storage& src, Storage* dest) noexcept {
            static_assert(std::is_nothrow_move_constructible_v<U>);
            U* object = std::launder(reinterpret_cast<U*>(src.buffer_.data()));
            // Make the array the active member. `std::start_lifetime_as` would
            // avoid zero-initializing it, but libc++ does not provide it.
            std::construct_at(&dest->buffer_);
            new (dest->buffer_.data()) U(std::move(*object));
          },
      .storage_kind = StorageKind::Buffer,
  };

  constexpr static VTable null_vtable{
      .destroy = +[](Storage&) noexcept {},
      .copy = +[](const Storage&, Storage*) noexcept {},
      .move = +[](Storage&, Storage*) noexcept {},
      .storage_kind = StorageKind::None,
  };

 public:
  template <typename U>
  explicit SBOWidePtr(U&& u, BufferStorageTag)
      : vtable_(&buffer_storage_vtable<std::remove_cvref_t<U>>) {
    using V = std::remove_cvref_t<U>;
    static_assert(sizeof(V) <= Storage::BufferSize);
    static_assert(alignof(V) <= alignof(Storage));
    // Make the array the active member. `std::start_lifetime_as` would avoid
    // zero-initializing it, but libc++ does not provide it.
    std::construct_at(&storage_.buffer_);
    new (storage_.buffer_.data()) V(std::forward<U>(u));
  }

  template <typename U>
  explicit SBOWidePtr(U&& u, IndirectStorageTag)
      : vtable_(&indirect_storage_vtable<std::remove_cvref_t<U>>) {
    storage_.ptr_ = new std::remove_cvref_t<U>(std::forward<U>(u));
  }

  SBOWidePtr(const SBOWidePtr& wptr) : vtable_(wptr.vtable_) {
    wptr.vtable_->copy(wptr.storage_, &storage_);
  }

  SBOWidePtr(SBOWidePtr&& wptr) noexcept : vtable_(wptr.vtable_) {
    wptr.vtable_->move(wptr.storage_, &storage_);
    if (vtable_->storage_kind == StorageKind::Indirect) {
      wptr.vtable_ = &null_vtable;
    }
  }

  SBOWidePtr& operator=(const SBOWidePtr& wptr) {
    // Copy first so that a throwing copy leaves `*this` unchanged.
    return *this = SBOWidePtr(wptr);
  }

  SBOWidePtr& operator=(SBOWidePtr&& wptr) noexcept {
    if (&wptr == this) {
      return *this;
    }

    reset();
    wptr.vtable_->move(wptr.storage_, &storage_);
    vtable_ = wptr.vtable_;

    if (vtable_->storage_kind == StorageKind::Indirect) {
      wptr.vtable_ = &null_vtable;
    }

    return *this;
  }

  ~SBOWidePtr() { vtable_->destroy(storage_); }

  friend void swap(SBOWidePtr& lhs, SBOWidePtr& rhs) noexcept {
    // Move lhs into tmp.
    SBOWidePtr tmp(std::move(lhs));

    // Move rhs into lhs.
    lhs.reset();  // Needed for a moved-from buffer.
    rhs.vtable_->move(rhs.storage_, &lhs.storage_);
    lhs.vtable_ = rhs.vtable_;

    // Move tmp into rhs.
    rhs.reset();  // Needed for a moved-from buffer.
    tmp.vtable_->move(tmp.storage_, &rhs.storage_);
    rhs.vtable_ = tmp.vtable_;
  }

  void reset() {
    vtable_->destroy(storage_);
    vtable_ = &null_vtable;
  }

  StorageKind storage_kind() const { return vtable_->storage_kind; }
};

struct Tracker {
  struct Counts {
    std::size_t copies = 0;
    std::size_t moves = 0;
    std::size_t live_count = 0;

    Counts() = default;
    Counts(const Counts&) = delete ("Unused");
    Counts(Counts&&) = delete ("Unused");
    Counts& operator=(const Counts&) = delete ("Unused");
    Counts& operator=(Counts&&) = delete ("Unused");

    ~Counts() { EXPECT_EQ(live_count, 0); }
  };

  Counts* counts_;

  Tracker(Counts* counts) : counts_(counts) { ++counts_->live_count; }

  Tracker(const Tracker& other) : counts_(other.counts_) {
    ++counts_->live_count;
    ++other.counts_->copies;
  }

  Tracker(Tracker&& other) noexcept : counts_(other.counts_) {
    ++counts_->live_count;
    ++other.counts_->moves;
  }

  Tracker& operator=(const Tracker&) = delete ("Unused");

  Tracker& operator=(Tracker&&) = delete ("Unused");

  ~Tracker() { --counts_->live_count; }
};

TEST(TutorialsWidePointerSBO, ConstructIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
}

TEST(TutorialsWidePointerSBO, ConstructBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, MoveBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);

  counts.moves = 0;
  auto pp(std::move(p));

  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Buffer);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, MoveIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);

  counts.moves = 0;
  auto pp(std::move(p));

  EXPECT_EQ(counts.moves, 0);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Indirect);
  EXPECT_EQ(p.storage_kind(), StorageKind::None);
}

TEST(TutorialsWidePointerSBO, MoveEmpty) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  p.reset();

  SBOWidePtr pp(std::move(p));

  EXPECT_EQ(pp.storage_kind(), StorageKind::None);
}

TEST(TutorialsWidePointerSBO, CopyBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(p);

  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, CopyIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(p);

  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Indirect);
}

TEST(TutorialsWidePointerSBO, CopyEmpty) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  p.reset();

  SBOWidePtr pp(p);

  EXPECT_EQ(pp.storage_kind(), StorageKind::None);
}

TEST(TutorialsWidePointerSBO, SwapIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(Tracker(&counts), indirect_storage);

  counts.moves = 0;
  swap(p, pp);

  EXPECT_EQ(counts.moves, 0);
  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Indirect);
}

TEST(TutorialsWidePointerSBO, SwapBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(Tracker(&counts), buffer_storage);

  counts.moves = 0;
  swap(p, pp);

  EXPECT_EQ(counts.moves, 3);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, SwapIndirectBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(Tracker(&counts), buffer_storage);

  counts.moves = 0;
  swap(p, pp);
  EXPECT_EQ(counts.moves, 1);

  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Indirect);
}

TEST(TutorialsWidePointerSBO, SwapBufferIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(Tracker(&counts), indirect_storage);

  counts.moves = 0;
  swap(p, pp);
  EXPECT_EQ(counts.moves, 2);

  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, AssignIndirectToIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(Tracker(&counts), indirect_storage);

  p = pp;
  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
}

TEST(TutorialsWidePointerSBO, AssignBufferToIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(Tracker(&counts), buffer_storage);

  p = pp;
  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, AssignIndirectToBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(Tracker(&counts), indirect_storage);

  p = pp;
  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
}

TEST(TutorialsWidePointerSBO, AssignBufferToBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(Tracker(&counts), buffer_storage);

  p = pp;
  EXPECT_EQ(counts.copies, 1);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, MoveAssignIndirectToIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(Tracker(&counts), indirect_storage);

  counts.moves = 0;
  p = std::move(pp);
  EXPECT_EQ(counts.moves, 0);
  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
  EXPECT_EQ(pp.storage_kind(), StorageKind::None);
}

TEST(TutorialsWidePointerSBO, MoveAssignBufferToIndirect) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), indirect_storage);
  SBOWidePtr pp(Tracker(&counts), buffer_storage);

  counts.moves = 0;
  p = std::move(pp);
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Buffer);
}

TEST(TutorialsWidePointerSBO, MoveAssignIndirectToBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(Tracker(&counts), indirect_storage);

  counts.moves = 0;
  p = std::move(pp);
  EXPECT_EQ(counts.moves, 0);
  EXPECT_EQ(p.storage_kind(), StorageKind::Indirect);
  EXPECT_EQ(pp.storage_kind(), StorageKind::None);
}

TEST(TutorialsWidePointerSBO, MoveAssignBufferToBuffer) {
  Tracker::Counts counts;

  SBOWidePtr p(Tracker(&counts), buffer_storage);
  SBOWidePtr pp(Tracker(&counts), buffer_storage);

  counts.moves = 0;
  p = std::move(pp);
  EXPECT_EQ(counts.moves, 1);
  EXPECT_EQ(p.storage_kind(), StorageKind::Buffer);
  EXPECT_EQ(pp.storage_kind(), StorageKind::Buffer);
}

}  // namespace xyz::tutorials::wide_pointer_small_buffer

// NOLINTEND(performance-unnecessary-copy-initialization)
// NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
// NOLINTEND(cppcoreguidelines-pro-type-union-access)

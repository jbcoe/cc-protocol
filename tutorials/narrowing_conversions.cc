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

#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// A follow-up to the tutorial on polymorphism. We construct a narrow
// type-erased pointer directly from a wide one, rather than from the
// concrete Duck or Eagle underneath it.

namespace xyz::tutorials::narrowing_a_wide_pointer {

class Duck {
 public:
  std::string_view noise() const { return "Quack"; }

  std::string_view identity() const { return "Duck"; }
};

class Eagle {
 public:
  std::string_view noise() const { return "Screech"; }

  std::string_view identity() const { return "Eagle"; }
};

// BirdVtable is narrower than AnimalVtable in the same way a Bird is a
// narrower category than an Animal: every bird makes a noise, but not
// every animal has an identity() worth asking about here.
struct BirdVtable {
  std::string_view (*noise_func_)(const void* data);
};

struct AnimalVtable {
  std::string_view (*noise_func_)(const void* data);
  std::string_view (*identity_func_)(const void* data);
};

class AnimalPtr {
  void* data_;
  const AnimalVtable* vtable_;

  friend class BirdPtr;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    constexpr static AnimalVtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
        .identity_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->identity();
            },
    };
    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return vtable_->noise_func_(data_); }

  std::string_view identity() const { return vtable_->identity_func_(data_); }

  // Test-only: lets tests confirm bird_vtable_for's cache is keyed and
  // reused as intended.
  const AnimalVtable* vtable_for_testing() const { return vtable_; }
};

// Builds a BirdVtable from `animal_vtable`'s noise_func_ once per distinct
// `animal_vtable`, caching it for reuse: the cache grows to at most one
// entry per T ever narrowed, never more. Reads dominate over the rare
// first-use insert, so a shared_mutex lets concurrent reads proceed
// without blocking each other.
const BirdVtable* bird_vtable_for(const AnimalVtable* animal_vtable) {
  static std::shared_mutex mutex;
  static std::unordered_map<const AnimalVtable*, BirdVtable> cache;

  {
    const std::shared_lock read_lock(mutex);
    if (auto entry = cache.find(animal_vtable); entry != cache.end()) {
      return &entry->second;
    }
  }

  const std::unique_lock write_lock(mutex);
  auto [entry, inserted] = cache.try_emplace(animal_vtable);
  if (inserted) {
    entry->second.noise_func_ = animal_vtable->noise_func_;
  }
  return &entry->second;
}

class BirdPtr {
  void* data_;
  const BirdVtable* vtable_;

 public:
  // Handles a concrete type such as Duck or Eagle: T is deduced from
  // whatever pointer is passed in, and the vtable is a constexpr static
  // shared by every BirdPtr built from the same T.
  template <typename T>
  BirdPtr(T* t) : data_(t) {
    constexpr static BirdVtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
    };
    vtable_ = &vtable_for_type;
  }

  // Handles an AnimalPtr by looking up (or, on first use, building) a
  // cached BirdVtable for its vtable_, rather than owning one itself or
  // wrapping the AnimalPtr.
  BirdPtr(AnimalPtr* animal)
      : data_(animal->data_), vtable_(bird_vtable_for(animal->vtable_)) {}

  std::string_view noise() const { return vtable_->noise_func_(data_); }
};

std::string_view make_noise(BirdPtr bird) { return bird.noise(); }

static_assert(sizeof(BirdPtr) == 2 * sizeof(void*),
              "constructing from an AnimalPtr must not grow "
              "BirdPtr beyond its usual data+vtable pointer pair");

TEST(TutorialsNarrowingConversions, NarrowingAWidePointer) {
  Duck duck;
  AnimalPtr duck_view(&duck);

  EXPECT_EQ(duck_view.noise(), "Quack");
  EXPECT_EQ(duck_view.identity(), "Duck");

  // Constructed from a pointer to duck_view, not from `duck` itself.
  BirdPtr duck_as_bird(&duck_view);

  EXPECT_EQ(make_noise(duck_as_bird), "Quack");
}

TEST(TutorialsNarrowingConversions, NarrowingAWidePointerMemoizes) {
  Duck duck;
  AnimalPtr duck_view(&duck);
  Eagle eagle;
  AnimalPtr eagle_view(&eagle);

  // Two lookups against the same AnimalPtr share a cache entry; a
  // different AnimalPtr gets its own.
  EXPECT_EQ(bird_vtable_for(duck_view.vtable_for_testing()),
            bird_vtable_for(duck_view.vtable_for_testing()));
  EXPECT_NE(bird_vtable_for(duck_view.vtable_for_testing()),
            bird_vtable_for(eagle_view.vtable_for_testing()));
}

TEST(TutorialsNarrowingConversions, NarrowingAWidePointerConcurrently) {
  Duck duck;
  AnimalPtr duck_view(&duck);
  Eagle eagle;
  AnimalPtr eagle_view(&eagle);

  // Enough threads per bird to make first-use races over the same cache
  // entry likely, without turning this into a long-running stress test.
  constexpr int kThreadsPerBird = 8;
  std::vector<std::jthread> threads;
  threads.reserve(2 * kThreadsPerBird);
  for (int i = 0; i < kThreadsPerBird; ++i) {
    threads.emplace_back([&duck_view] {
      BirdPtr bird(&duck_view);
      EXPECT_EQ(bird.noise(), "Quack");
    });
    threads.emplace_back([&eagle_view] {
      BirdPtr bird(&eagle_view);
      EXPECT_EQ(bird.noise(), "Screech");
    });
  }
  // jthreads join automatically when destroyed, so no explicit join loop.
}

}  // namespace xyz::tutorials::narrowing_a_wide_pointer

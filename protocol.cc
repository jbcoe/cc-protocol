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

#include "protocol.h"

#include <cassert>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace xyz {
namespace {

// The CacheKey uniquely identifies a specific type conversion mapping.
// It consists of:
// 1. source_vtable_pointer: Points to the source vtable (identifies the
// concrete type).
// 2. conversion_anchor: Points to a static tag unique to the (From, To)
// template
//    instantiation. This is required because a single source vtable can be
//    converted to multiple different target protocols with different vtable
//    layouts, so we cannot index the cache by the source vtable address alone.
struct CacheKey {
  const void* source_vtable_pointer;
  const void* conversion_anchor;

  bool operator==(const CacheKey&) const = default;
};

struct CacheKeyHash {
  std::size_t operator()(const CacheKey& key) const {
    std::size_t hash_value_1 =
        std::hash<const void*>{}(key.source_vtable_pointer);
    std::size_t hash_value_2 = std::hash<const void*>{}(key.conversion_anchor);
    return hash_value_1 ^ (hash_value_2 + 0x9e3779b9 + (hash_value_1 << 6) +
                           (hash_value_1 >> 2));
  }
};

}  // namespace

const void* get_mapped_vtable(const void* source_vtable_pointer,
                              const void* conversion_anchor,
                              std::size_t target_vtable_size,
                              void (*mapping_function)(const void* source,
                                                       void* target)) {
  assert(source_vtable_pointer != nullptr);

  // The cache map and its protecting mutex are allocated on the heap via 'new'
  // and intentionally leaked (never destroyed). This prevents exit-time
  // destruction order bugs (static destruction order fiasco) if other global
  // or static objects trigger protocol conversions during program shutdown
  // cleanup.
  //
  // Values are stored as std::unique_ptr<char[]> to provide:
  // 1. Dynamic sizing: Target vtable sizes are only known at runtime.
  // 2. Pointer stability: Returns raw pointers that must remain valid for the
  //    lifetime of the application; heap allocation in std::unique_ptr ensures
  //    rehashing or modifying the map does not invalidate returned pointers.
  static auto& cache =
      *new std::unordered_map<CacheKey, std::unique_ptr<char[]>,
                              CacheKeyHash>();
  static auto& mutex = *new std::mutex();

  CacheKey key{source_vtable_pointer, conversion_anchor};
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto cache_iterator = cache.find(key);
    if (cache_iterator != cache.end()) {
      return cache_iterator->second.get();
    }
  }

  auto vtable_data = std::make_unique<char[]>(target_vtable_size);
  mapping_function(source_vtable_pointer, vtable_data.get());

  std::lock_guard<std::mutex> lock(mutex);
  // Under the split-lock pattern, another thread might have inserted the key
  // concurrently while we were mapping the vtable. std::unordered_map::emplace
  // does not overwrite an existing value, so if a collision occurs, the
  // existing vtable is kept, our local copy is discarded, and we return the
  // stable cached pointer.
  auto [inserted_iterator, inserted] =
      cache.emplace(key, std::move(vtable_data));
  return inserted_iterator->second.get();
}

}  // namespace xyz

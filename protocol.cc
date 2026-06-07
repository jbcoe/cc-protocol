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

#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace xyz {
namespace {

struct CacheKey {
  const void* from_vptr;
  const void* type_id;

  bool operator==(const CacheKey&) const = default;
};

struct CacheKeyHash {
  std::size_t operator()(const CacheKey& key) const {
    std::size_t h1 = std::hash<const void*>{}(key.from_vptr);
    std::size_t h2 = std::hash<const void*>{}(key.type_id);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
  }
};

}  // namespace

const void* get_or_create_vtable_erased(
    const void* from_vptr, const void* type_id, std::size_t to_vtable_size,
    void (*mapper)(const void* from, void* to)) {
  if (from_vptr == nullptr) {
    return nullptr;
  }

  static auto& cache =
      *new std::unordered_map<CacheKey, std::unique_ptr<char[]>,
                              CacheKeyHash>();
  static auto& mutex = *new std::mutex();

  CacheKey key{from_vptr, type_id};
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(key);
    if (it != cache.end()) {
      return it->second.get();
    }
  }

  auto vtable_data = std::make_unique<char[]>(to_vtable_size);
  mapper(from_vptr, vtable_data.get());

  std::lock_guard<std::mutex> lock(mutex);
  auto [inserted_it, inserted] = cache.emplace(key, std::move(vtable_data));
  return inserted_it->second.get();
}

}  // namespace xyz

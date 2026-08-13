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

#include <cassert>
#include <concepts>
#include <memory>
#include <utility>

// An exploration of adding allocator support to an owning type.

// Owner is a class that simply holds an object of type T. It
// does not support anything besides the rule-of-five member functions
// and swap. This class is for demonstration purposes only.
namespace xyz::tutorials::owning_type {

template <typename T>
class Owner {
  // The managed resource. We leave it as null if Owner is default
  // constructed or moved-from.
  T* obj_ = nullptr;

  static void destroy(T* obj) { delete obj; }

  static T* copy(const T* obj) {
    if (obj == nullptr) {
      return nullptr;
    }
    return new T(*obj);
  }

 public:
  Owner() = default;

  explicit Owner(const T& obj) : obj_(new T(obj)) {}

  explicit Owner(T&& obj) : obj_(new T(std::move(obj))) {}

  // Rule-of-five implementation below.

  ~Owner() { destroy(obj_); }

  Owner(const Owner& other) : obj_(copy(other.obj_)) {}

  Owner& operator=(const Owner& other) {
    if (this != &other) {
      destroy(obj_);
      obj_ = copy(other.obj_);
    }
    return *this;
  }

  Owner(Owner&& other) noexcept : obj_(std::exchange(other.obj_, nullptr)) {}

  Owner& operator=(Owner&& other) noexcept {
    if (this != &other) {
      destroy(obj_);
      obj_ = std::exchange(other.obj_, nullptr);
    }
    return *this;
  }

  // A swap member function to prevent redundant moves
  // when swapping.
  void swap(Owner& other) noexcept { std::swap(obj_, other.obj_); }

  bool has_value() const { return obj_ != nullptr; }

  T& get() {
    assert(has_value());
    return *obj_;
  }

  const T& get() const {
    assert(has_value());
    return *obj_;
  }
};

TEST(TutorialsAllocators, OwningType) {
  Owner<int> o1;
  EXPECT_FALSE(o1.has_value());

  Owner<int> o2{10};
  EXPECT_TRUE(o2.has_value());
  EXPECT_EQ(o2.get(), 10);

  Owner o3{std::move(o2)};
  EXPECT_FALSE(o2.has_value());
  EXPECT_EQ(o3.get(), 10);

  o3 = o1;
  EXPECT_FALSE(o3.has_value());
}

}  // namespace xyz::tutorials::owning_type

// Next, we add very rudimentary support for allocators. We do
// not expose it anywhere in the public interface, aside from
// the template argument; instead, we require that the allocator
// type is default-constructible (stateless).
namespace xyz::tutorials::simple_allocator {

template <typename T, typename Alloc = std::allocator<T>>
  requires std::default_initializable<Alloc>
class Owner {
  [[no_unique_address]] Alloc alloc_;
  T* obj_ = nullptr;

  // The allocator_traits struct provides a useful wrapper
  // around the provided allocator type. It gives us an easy
  // way to allocate, copy, etc.
  using traits = std::allocator_traits<Alloc>;

  // We use auto&& so this works for both lvalue and rvalue construction.
  T* create(auto&& source) {
    T* obj = traits::allocate(alloc_, 1);  // Allocate room for one T
    // We must make sure to reclaim the memory from
    // the previous line in case the constructor throws.
    try {
      traits::construct(alloc_, obj, std::forward<decltype(source)>(source));
    } catch (...) {
      traits::deallocate(alloc_, obj, 1);
      throw;
    }

    return obj;
  }

  void destroy(T* obj) {
    if (obj == nullptr) {
      return;
    }
    traits::destroy(alloc_, obj);
    traits::deallocate(alloc_, obj, 1);
  }

  T* copy(const T* obj) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(*obj);
  }

 public:
  Owner() = default;

  explicit Owner(const T& obj) : obj_(create(obj)) {}

  explicit Owner(T&& obj) : obj_(create(std::move(obj))) {}

  // Everything else about the implementation is the same;
  // the new versions of copy and destroy do the work.

  ~Owner() { destroy(obj_); }

  Owner(const Owner& other) : obj_(copy(other.obj_)) {}

  Owner& operator=(const Owner& other) {
    if (this != &other) {
      destroy(obj_);
      obj_ = copy(other.obj_);
    }
    return *this;
  }

  Owner(Owner&& other) noexcept : obj_(std::exchange(other.obj_, nullptr)) {}

  Owner& operator=(Owner&& other) noexcept {
    if (this != &other) {
      destroy(obj_);
      obj_ = std::exchange(other.obj_, nullptr);
    }
    return *this;
  }

  // A swap member function to prevent redundant moves
  // when swapping.
  void swap(Owner& other) noexcept { std::swap(obj_, other.obj_); }

  bool has_value() const { return obj_ != nullptr; }

  T& get() {
    assert(has_value());
    return *obj_;
  }

  const T& get() const {
    assert(has_value());
    return *obj_;
  }

  const Alloc& get_allocator() const { return alloc_; }
};

// We now test the exact same behavior, this time using the
// allocator-aware version of Owner and std::allocator<int>.
TEST(TutorialsAllocators, SimpleAllocatorOwner) {
  Owner<int> o1;
  EXPECT_EQ(o1.get_allocator(), std::allocator<int>{});
  EXPECT_FALSE(o1.has_value());

  Owner<int> o2{10};
  EXPECT_EQ(o2.get_allocator(), std::allocator<int>{});
  EXPECT_TRUE(o2.has_value());
  EXPECT_EQ(o2.get(), 10);

  Owner o3{std::move(o2)};
  EXPECT_FALSE(o2.has_value());
  EXPECT_EQ(o3.get(), 10);

  o3 = o1;
  EXPECT_FALSE(o3.has_value());

  // All of the memory gets cleaned up at the end of scope.
}

}  // namespace xyz::tutorials::simple_allocator

// Now, let's allow allocators that hold state, and add
// allocator-aware constructors. For now, we will remove
// the ability for Owner to be move assigned or swapped.
// We will discuss the complexity of adding these features in the next section.
namespace xyz::tutorials::stateful_allocator {

template <typename T, typename Alloc = std::allocator<T>>
class Owner {
  // We still use no_unique_address because we can still accept
  // stateless allocators.
  [[no_unique_address]] Alloc alloc_;
  T* obj_ = nullptr;

  using traits = std::allocator_traits<Alloc>;

  // create, destroy, and copy are the same as before.

  T* create(auto&& source) {
    T* obj = traits::allocate(alloc_, 1);
    try {
      traits::construct(alloc_, obj, std::forward<decltype(source)>(source));
    } catch (...) {
      traits::deallocate(alloc_, obj, 1);
      throw;
    }

    return obj;
  }

  void destroy(T* obj) {
    if (obj == nullptr) {
      return;
    }
    traits::destroy(alloc_, obj);
    traits::deallocate(alloc_, obj, 1);
  }

  T* copy(const T* obj) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(*obj);
  }

 public:
  Owner()
    requires std::default_initializable<Alloc>
  = default;

  explicit Owner(const T& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(obj)) {}

  explicit Owner(T&& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(std::move(obj))) {}

  // There are two styles for constructors accepting allocator arguments:
  // a trailing allocator parameter, i.e. Owner(const T&, Alloc), and
  // the style shown below, tagged with allocator_arg_t. The following style
  // works with variadic constructors, whereas the trailing style does not.
  explicit Owner(std::allocator_arg_t, const Alloc& a, const T& obj)
      : alloc_(a), obj_(create(obj)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a), obj_(create(std::move(obj))) {}

  ~Owner() { destroy(obj_); }

  // Allocators can define how they should be copied when the
  // container is copied. select_on_container_copy_construction will dispatch
  // to the allocator's unique copy function, or perform a regular copy if it
  // is not defined.
  Owner(const Owner& other)
      : alloc_(traits::select_on_container_copy_construction(other.alloc_)),
        obj_(copy(other.obj_)) {}

  // Allocator-aware copy construction.
  Owner(std::allocator_arg_t, const Alloc& a, const Owner& other)
      : alloc_(a), obj_(copy(other.obj_)) {}

  Owner(Owner&& other) noexcept
      : alloc_(std::move(other.alloc_)),
        obj_(std::exchange(other.obj_, nullptr)) {}

  // Allocator-aware move construction is complex - see the following step.
  Owner(std::allocator_arg_t, const Alloc& a, Owner&& other) = delete;

  // Similarly to copy construction, allocators can define whether they should
  // propagate on copy assignment. We manually check this property below, and
  // only reassign our allocator if it is true.
  Owner& operator=(const Owner& other) {
    if (this != &other) {
      destroy(obj_);  // Destroy with the old allocator.
      if constexpr (traits::propagate_on_container_copy_assignment::value) {
        alloc_ = other.alloc_;
      }
      obj_ = copy(other.obj_);  // Construct with the new allocator.
    }
    return *this;
  }

  Owner& operator=(Owner&& other) = delete;

  void swap(Owner& other) = delete;

  bool has_value() const { return obj_ != nullptr; }

  T& get() {
    assert(has_value());
    return *obj_;
  }

  const T& get() const {
    assert(has_value());
    return *obj_;
  }

  const Alloc& get_allocator() const { return alloc_; }
};

// To demonstrate that our new Owner works, we will make a simple stateful
// allocator.
template <typename T>
class TestAlloc {
 public:
  using value_type = T;
  int tag = 0;  // Our "state"

  TestAlloc() = default;

  explicit TestAlloc(int tag) : tag(tag) {}

  T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }

  void deallocate(T* ptr, std::size_t count) {
    std::allocator<T>{}.deallocate(ptr, count);
  }

  bool operator==(const TestAlloc&) const = default;
};

TEST(TutorialsAllocators, StatefulAllocator) {
  using TestOwner = Owner<int, TestAlloc<int>>;

  // Default constructs the allocator.
  TestOwner o1;
  EXPECT_EQ(o1.get_allocator().tag, 0);

  // Default constructs the allocator, even though we provide a value.
  TestOwner o2{42};
  EXPECT_EQ(o2.get(), 42);
  EXPECT_EQ(o2.get_allocator().tag, 0);

  // Provides an explicit allocator.
  TestOwner o3{std::allocator_arg, TestAlloc<int>{5}, 10};
  EXPECT_EQ(o3.get(), 10);               // 10 is the value stored in Owner.
  EXPECT_EQ(o3.get_allocator().tag, 5);  // 5 is the value stored in TestAlloc.

  // Copy assignment does NOT take the allocator by default.
  // If our allocator set propagate_on_container_copy_assignment to true,
  // it would take o3's allocator.
  o2 = o3;
  EXPECT_EQ(o2.get(), 10);
  EXPECT_EQ(o2.get_allocator().tag, 0);

  // Copy construction copies the allocator.
  TestOwner o4{o3};
  EXPECT_EQ(o4.get(), 10);
  // If TestAlloc had a unique select_on_container_copy_construction function,
  // the tag could differ here.
  EXPECT_EQ(o4.get_allocator().tag, 5);

  // Allocator-aware copy construction uses the new allocator.
  TestOwner o5{std::allocator_arg, TestAlloc<int>{20}, o3};
  EXPECT_EQ(o5.get(), 10);                // Take o3's value.
  EXPECT_EQ(o5.get_allocator().tag, 20);  // Use the new allocator.

  // Move construction takes the allocator.
  TestOwner o6{std::move(o5)};
  EXPECT_EQ(o6.get(), 10);
  EXPECT_EQ(o6.get_allocator().tag, 20);
}

}  // namespace xyz::tutorials::stateful_allocator

// Finally, we can add the missing operations back to Owner.
// An object allocated with one allocator must be deallocated
// with an equivalent allocator. We must pay careful attention
// to this fact when implementing moves and swaps.
namespace xyz::tutorials::propagating_allocator {

template <typename T, typename Alloc = std::allocator<T>>
class Owner {
  [[no_unique_address]] Alloc alloc_;
  T* obj_ = nullptr;

  using traits = std::allocator_traits<Alloc>;
  // Some useful shortenings for common properties of our allocator.
  constexpr static bool pocma =
      traits::propagate_on_container_move_assignment::value;
  constexpr static bool pocs = traits::propagate_on_container_swap::value;
  constexpr static bool always_equal = traits::is_always_equal::value;

  // create, destroy, and copy are the same as before.

  T* create(auto&& source) {
    T* obj = traits::allocate(alloc_, 1);
    try {
      traits::construct(alloc_, obj, std::forward<decltype(source)>(source));
    } catch (...) {
      traits::deallocate(alloc_, obj, 1);
      throw;
    }

    return obj;
  }

  void destroy(T* obj) {
    if (obj == nullptr) {
      return;
    }
    traits::destroy(alloc_, obj);
    traits::deallocate(alloc_, obj, 1);
  }

  T* copy(const T* obj) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(*obj);
  }

  // We may have to actually perform a move
  // for some cases now. This move constructs
  // a new T using our allocator (NOT a simple
  // pointer swap).
  T* move(T* obj) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(std::move(*obj));
  }

 public:
  Owner()
    requires std::default_initializable<Alloc>
  = default;

  explicit Owner(const T& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(obj)) {}

  explicit Owner(T&& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(std::move(obj))) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, const T& obj)
      : alloc_(a), obj_(create(obj)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a), obj_(create(std::move(obj))) {}

  ~Owner() { destroy(obj_); }

  Owner(const Owner& other)
      : alloc_(traits::select_on_container_copy_construction(other.alloc_)),
        obj_(copy(other.obj_)) {}

  Owner(std::allocator_arg_t, const Alloc& a, const Owner& other)
      : alloc_(a), obj_(copy(other.obj_)) {}

  Owner(Owner&& other) noexcept
      : alloc_(std::move(other.alloc_)),
        obj_(std::exchange(other.obj_, nullptr)) {}

  // Allocator-aware move construction. We can only statically guarantee
  // noexcept if the allocators are always equal. Otherwise, we may have to
  // perform a potentially-throwing allocation.
  Owner(std::allocator_arg_t, const Alloc& a,
        Owner&& other) noexcept(always_equal)
      : alloc_(a) {
    if (always_equal || alloc_ == other.alloc_) {
      // Fast path: alloc_ can deallocate other.obj_ since it is
      // equivalent to other.alloc_.
      obj_ = std::exchange(other.obj_, nullptr);
    } else {
      // Slow path: alloc_ and other.alloc_ are not equivalent.
      // We must newly move-construct our object.
      obj_ = move(other.obj_);
      other.destroy(other.obj_);
      other.obj_ = nullptr;
    }
  }

  Owner& operator=(const Owner& other) {
    if (this != &other) {
      destroy(obj_);
      if constexpr (traits::propagate_on_container_copy_assignment::value) {
        alloc_ = other.alloc_;
      }
      obj_ = copy(other.obj_);
    }
    return *this;
  }

  // See the code below for a justification of the noexcept specification.
  Owner& operator=(Owner&& other) noexcept(always_equal || pocma) {
    if (this != &other) {
      destroy(obj_);  // Deallocate with the old allocator.

      if constexpr (pocma) {
        alloc_ = other.alloc_;
      }

      // When pocma is true, we propagate the allocator. It is therefore
      // safe in that case to deallocate the original object with the new
      // allocator. Otherwise, we must verify the allocators are always_equal or
      // equivalent.
      if (always_equal || pocma || alloc_ == other.alloc_) {
        // Fast path.
        obj_ = std::exchange(other.obj_, nullptr);
      } else {
        // Slow path: we must newly move-construct our object.
        obj_ = move(other.obj_);
        other.destroy(other.obj_);
        other.obj_ = nullptr;
      }
    }
    return *this;
  }

  // See the code below for a justification of the noexcept specification.
  void swap(Owner& other) noexcept(always_equal || pocs) {
    // Unlike move assignment, swap will NOT attempt to reallocate if the
    // allocators are not equal. Instead, we require that the allocators
    // are equivalent - if they are not, swaping is undefined behavior.
    if constexpr (!always_equal && !pocs) {
      assert(alloc_ == other.alloc_);
    }

    using std::swap;       // Prefer a user-defined swap function when possible.
    if constexpr (pocs) {  // Propagate allocators on swap.
      swap(alloc_, other.alloc_);
    }
    swap(obj_, other.obj_);
  }

  bool has_value() const { return obj_ != nullptr; }

  T& get() {
    assert(has_value());
    return *obj_;
  }

  const T& get() const {
    assert(has_value());
    return *obj_;
  }

  const Alloc& get_allocator() const { return alloc_; }
};

}  // namespace xyz::tutorials::propagating_allocator
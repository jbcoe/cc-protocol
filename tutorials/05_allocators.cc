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
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // tutorial demonstrates the moved-from state on purpose.
  EXPECT_FALSE(o2.has_value());
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  EXPECT_EQ(o3.get(), 10);

  o3 = o1;
  EXPECT_FALSE(o3.has_value());
}

}  // namespace xyz::tutorials::owning_type

// Next, we add very rudimentary support for allocators. We begin by
// implementing the private functions and constructors. Some member
// functions have been deleted and will be discussed in further steps.
namespace xyz::tutorials::constructors {

template <typename T, typename Alloc = std::allocator<T>>
class Owner {
  // The allocator_traits struct provides a useful wrapper
  // around the provided allocator type. It gives us an easy
  // way to allocate, copy, etc.
  using traits = std::allocator_traits<Alloc>;

  // In some exotic cases, users may define a different pointer
  // type for their allocator than T*. We derive all pointers
  // from this typedef for full compatability.
  using pointer = traits::pointer;
  using const_pointer = traits::const_pointer;

  // We use no_unique_address to reduce Owner's memory footprint
  // when Alloc has zero size.
  [[no_unique_address]] Alloc alloc_;
  pointer obj_ = nullptr;

  // We use auto&& so this works for both lvalue and rvalue construction.
  static pointer create(auto&& source, Alloc& alloc) {
    pointer obj = traits::allocate(alloc, 1);  // Allocate room for one T.
    // We must make sure to reclaim the memory from
    // the previous line in case the constructor throws.
    try {
      traits::construct(alloc, obj, std::forward<decltype(source)>(source));
    } catch (...) {
      traits::deallocate(alloc, obj, 1);
      throw;
    }

    return obj;
  }

  static void destroy(pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return;
    }
    traits::destroy(alloc, obj);
    traits::deallocate(alloc, obj, 1);
  }

  static pointer copy(const_pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(*obj, alloc);
  }

 public:
  // This explicitly opts us into allocator support, so the standard library can
  // interface with Owner correctly.
  using allocator_type = Alloc;

  Owner()
    requires std::default_initializable<Alloc>
  = default;

  explicit Owner(const T& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(obj, alloc_)) {}

  explicit Owner(T&& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(std::move(obj), alloc_)) {}

  // There are two styles for constructors accepting allocator arguments:
  // a trailing allocator parameter, i.e. Owner(const T&, Alloc), and
  // the style shown below, tagged with allocator_arg_t. The following style
  // works with variadic constructors, whereas the trailing style does not.
  explicit Owner(std::allocator_arg_t, const Alloc& a, const T& obj)
      : alloc_(a), obj_(create(obj, alloc_)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a), obj_(create(std::move(obj), alloc_)) {}

  ~Owner() { destroy(obj_, alloc_); }

  // Allocators can define how they should be copied when the
  // container is copied. select_on_container_copy_construction will dispatch
  // to the allocator's unique copy function, or perform a regular copy if it
  // is not defined.
  Owner(const Owner& other)
      : alloc_(traits::select_on_container_copy_construction(other.alloc_)),
        obj_(copy(other.obj_, alloc_)) {}

  // Allocator-aware copy construction.
  Owner(std::allocator_arg_t, const Alloc& a, const Owner& other)
      : alloc_(a), obj_(copy(other.obj_, alloc_)) {}

  Owner(Owner&& other) noexcept
      : alloc_(std::move(other.alloc_)),
        obj_(std::exchange(other.obj_, nullptr)) {}

  // The following operations are discussed in further steps.

  Owner(std::allocator_arg_t, const Alloc& a, Owner&& other) = delete;

  Owner& operator=(const Owner& other) = delete;

  Owner& operator=(Owner&& other) noexcept = delete;

  void swap(Owner& other) noexcept = delete;

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

// We first test the old constructors, this time using the
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
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // tutorial demonstrates the moved-from state on purpose.
  EXPECT_FALSE(o2.has_value());
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  EXPECT_EQ(o3.get(), 10);

  // All of the memory gets cleaned up at the end of scope.
}

// To demonstrate that our new Owner works, we will make a simple stateful
// allocator.
template <typename T>
class TestAlloc {
 public:
  using value_type = T;
  int tag = 0;  // Our "state".

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

  // Default constructs the allocator.
  TestOwner o2{42};
  EXPECT_EQ(o2.get(), 42);
  EXPECT_EQ(o2.get_allocator().tag, 0);

  // Provides an explicit allocator.
  TestOwner o3{std::allocator_arg, TestAlloc<int>{5}, 10};
  EXPECT_EQ(o3.get(), 10);               // 10 is the value stored in Owner.
  EXPECT_EQ(o3.get_allocator().tag, 5);  // 5 is the value stored in TestAlloc.

  // Copy construction copies the allocator; the copy is what this example
  // demonstrates.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
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

}  // namespace xyz::tutorials::constructors

// Now, we can add the missing operations back to Owner.
// An object allocated with one allocator must be deallocated
// with an equivalent allocator. We must pay careful attention
// to this fact when implementing moves and swaps.
namespace xyz::tutorials::propagating_allocator {

template <typename T, typename Alloc = std::allocator<T>>
class Owner {
  using traits = std::allocator_traits<Alloc>;

  using pointer = traits::pointer;
  using const_pointer = traits::const_pointer;

  [[no_unique_address]] Alloc alloc_;
  pointer obj_ = nullptr;

  // Some useful shortenings for common properties of our allocator.
  constexpr static bool pocca =
      traits::propagate_on_container_copy_assignment::value;
  constexpr static bool pocma =
      traits::propagate_on_container_move_assignment::value;
  constexpr static bool pocs = traits::propagate_on_container_swap::value;
  constexpr static bool always_equal = traits::is_always_equal::value;

  // create, destroy, and copy are the same as before.

  static pointer create(auto&& source, Alloc& alloc) {
    pointer obj = traits::allocate(alloc, 1);
    try {
      traits::construct(alloc, obj, std::forward<decltype(source)>(source));
    } catch (...) {
      traits::deallocate(alloc, obj, 1);
      throw;
    }

    return obj;
  }

  static void destroy(pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return;
    }
    traits::destroy(alloc, obj);
    traits::deallocate(alloc, obj, 1);
  }

  static pointer copy(const_pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(*obj, alloc);
  }

  // We may have to actually perform a move
  // for some cases now. This move constructs
  // a new T using our allocator (NOT a simple
  // pointer swap).
  static pointer move(pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(std::move(*obj), alloc);
  }

 public:
  using allocator_type = Alloc;

  Owner()
    requires std::default_initializable<Alloc>
  = default;

  explicit Owner(const T& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(obj, alloc_)) {}

  explicit Owner(T&& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(std::move(obj), alloc_)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, const T& obj)
      : alloc_(a), obj_(create(obj, alloc_)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a), obj_(create(std::move(obj), alloc_)) {}

  ~Owner() { destroy(obj_, alloc_); }

  Owner(const Owner& other)
      : alloc_(traits::select_on_container_copy_construction(other.alloc_)),
        obj_(copy(other.obj_, alloc_)) {}

  Owner(std::allocator_arg_t, const Alloc& a, const Owner& other)
      : alloc_(a), obj_(copy(other.obj_, alloc_)) {}

  Owner(Owner&& other) noexcept
      : alloc_(std::move(other.alloc_)),
        obj_(std::exchange(other.obj_, nullptr)) {}

  // Allocator-aware move construction. We can only statically guarantee
  // noexcept if the allocators are always equal. Otherwise, we may have to
  // perform a potentially-throwing allocation.
  //
  // The resources of `other` are taken with std::exchange rather than
  // std::move, which the rvalue-reference-param-not-moved check does not
  // recognise.
  // NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved)
  Owner(std::allocator_arg_t, const Alloc& a,
        Owner&& other) noexcept(always_equal)
      : alloc_(a) {
    // NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved)
    if (always_equal || alloc_ == other.alloc_) {
      // Fast path: alloc_ can deallocate other.obj_ since it is
      // equivalent to other.alloc_.
      obj_ = std::exchange(other.obj_, nullptr);
    } else {
      // Slow path: alloc_ and other.alloc_ are not equivalent.
      // We must newly move-construct our object.
      obj_ = move(other.obj_, alloc_);
      destroy(other.obj_, other.alloc_);
      other.obj_ = nullptr;
    }
  }

  // Allocators can define whether they should propagate on copy assignment. We
  // manually check this property below, and only reassign our allocator if it
  // is true.
  Owner& operator=(const Owner& other) {
    if (this != &other) {
      destroy(obj_, alloc_);  // Destroy with the old allocator.
      if constexpr (pocca) {
        alloc_ = other.alloc_;
      }
      obj_ = copy(other.obj_, alloc_);  // Construct with the new allocator.
    }
    return *this;
  }

  // See the code below for a justification of the noexcept specification.
  // NOLINTBEGIN(bugprone-exception-escape): conditionally noexcept, modelled
  // on allocator-aware standard containers; the potentially throwing copy
  // path is taken only when the noexcept condition is false.
  Owner& operator=(Owner&& other) noexcept(always_equal || pocma) {
    // NOLINTEND(bugprone-exception-escape)
    if (this != &other) {
      destroy(obj_, alloc_);  // Destroy with the old allocator.

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
        obj_ = move(other.obj_, alloc_);
        destroy(other.obj_, other.alloc_);
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

// Take TestAlloc from the previous section.
using constructors::TestAlloc;

// A variant of TestAlloc that requires propagation on move assignment.
template <typename T>
class PocmaAlloc : public TestAlloc<T> {
 public:
  using TestAlloc<T>::TestAlloc;
  using propagate_on_container_move_assignment = std::true_type;
};

// A variant of TestAlloc that requires propagation on swap.
template <typename T>
class PocsAlloc : public TestAlloc<T> {
 public:
  using TestAlloc<T>::TestAlloc;
  using propagate_on_container_swap = std::true_type;
};

TEST(TutorialsAllocators, CopyAssignment) {
  using TestOwner = Owner<int, TestAlloc<int>>;

  TestOwner o1{std::allocator_arg, TestAlloc<int>{0}, 10};
  TestOwner o2{20};
  // Copy assignment does NOT take the allocator by default.
  // If our allocator set propagate_on_container_copy_assignment to true,
  // it would take o2's allocator.
  o1 = o2;
  EXPECT_EQ(o1.get(), 20);
  EXPECT_EQ(o1.get_allocator().tag, 0);
}

TEST(TutorialsAllocators, PropagatingAllocatorMoves) {
  using TestOwner = Owner<int, TestAlloc<int>>;

  // Because TestAlloc does not define pocma or is_always_equal as
  // true, moving is not always noexcept.
  static_assert(!std::is_nothrow_move_assignable_v<TestOwner>);

  // Allocator-aware move construction.
  TestOwner o1{std::allocator_arg, TestAlloc<int>{1}, 10};
  // An allocation is performed when o2 is constructed.
  TestOwner o2{std::allocator_arg, TestAlloc<int>{2}, std::move(o1)};
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // tutorial demonstrates the moved-from state on purpose.
  EXPECT_FALSE(o1.has_value());
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  EXPECT_EQ(o2.get(), 10);
  EXPECT_EQ(o2.get_allocator().tag, 2);

  // Move assignment with unequal allocators.
  TestOwner o3{std::allocator_arg, TestAlloc<int>{3}, 10};
  TestOwner o4{std::allocator_arg, TestAlloc<int>{4}, 20};
  // o3 will allocate space for a new int instead of reusing o4's object.
  o3 = std::move(o4);
  EXPECT_EQ(o3.get(), 20);

  using PocmaOwner = Owner<int, PocmaAlloc<int>>;
  PocmaOwner o5{std::allocator_arg, PocmaAlloc<int>{5}, 10};
  PocmaOwner o6{std::allocator_arg, PocmaAlloc<int>{6}, 20};
  o5 = std::move(o6);
  // Move assignment now propagates the allocator.
  EXPECT_EQ(o5.get_allocator().tag, 6);
}

TEST(TutorialsAllocators, PropagatingAllocatorSwaps) {
  using TestOwner = Owner<int, TestAlloc<int>>;

  TestOwner o1{std::allocator_arg, TestAlloc<int>{1}, 10};
  TestOwner o2{std::allocator_arg, TestAlloc<int>{1}, 20};
  // If the allocators were not equal, this would be UB.
  o1.swap(o2);
  EXPECT_EQ(o1.get(), 20);
  EXPECT_EQ(o2.get(), 10);

  using PocsOwner = Owner<int, PocsAlloc<int>>;

  PocsOwner o3{std::allocator_arg, PocsAlloc<int>{3}, 10};
  PocsOwner o4{std::allocator_arg, PocsAlloc<int>{4}, 20};
  o3.swap(o4);
  EXPECT_EQ(o3.get_allocator().tag, 4);
  EXPECT_EQ(o4.get_allocator().tag, 3);
}

}  // namespace xyz::tutorials::propagating_allocator

// In the above example, copy and move assignment only provide the basic
// exception guarantee, which states that the involved objects will be
// left in a valid-but-unspecified state if an exception is thrown. Here,
// we will explore adding a strong exception guarantee to these functions,
// which states that all involved objects are returned to their original
// state in case of an exception.
namespace xyz::tutorials::strong_guarantee {

// Everything here is the same except copy and move assignment.
template <typename T, typename Alloc = std::allocator<T>>
class Owner {
  using traits = std::allocator_traits<Alloc>;

  using pointer = traits::pointer;
  using const_pointer = traits::const_pointer;

  [[no_unique_address]] Alloc alloc_;
  pointer obj_ = nullptr;

  constexpr static bool pocca =
      traits::propagate_on_container_copy_assignment::value;
  constexpr static bool pocma =
      traits::propagate_on_container_move_assignment::value;
  constexpr static bool pocs = traits::propagate_on_container_swap::value;
  constexpr static bool always_equal = traits::is_always_equal::value;

  static pointer create(auto&& source, Alloc& alloc) {
    pointer obj = traits::allocate(alloc, 1);
    try {
      traits::construct(alloc, obj, std::forward<decltype(source)>(source));
    } catch (...) {
      traits::deallocate(alloc, obj, 1);
      throw;
    }

    return obj;
  }

  static void destroy(pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return;
    }
    traits::destroy(alloc, obj);
    traits::deallocate(alloc, obj, 1);
  }

  static pointer copy(const_pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(*obj, alloc);
  }

  static pointer move(pointer obj, Alloc& alloc) {
    if (obj == nullptr) {
      return nullptr;
    }
    return create(std::move(*obj), alloc);
  }

 public:
  using allocator_type = Alloc;

  Owner()
    requires std::default_initializable<Alloc>
  = default;

  explicit Owner(const T& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(obj, alloc_)) {}

  explicit Owner(T&& obj)
    requires std::default_initializable<Alloc>
      : obj_(create(std::move(obj), alloc_)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, const T& obj)
      : alloc_(a), obj_(create(obj, alloc_)) {}

  explicit Owner(std::allocator_arg_t, const Alloc& a, T&& obj)
      : alloc_(a), obj_(create(std::move(obj), alloc_)) {}

  ~Owner() { destroy(obj_, alloc_); }

  Owner(const Owner& other)
      : alloc_(traits::select_on_container_copy_construction(other.alloc_)),
        obj_(copy(other.obj_, alloc_)) {}

  Owner(std::allocator_arg_t, const Alloc& a, const Owner& other)
      : alloc_(a), obj_(copy(other.obj_, alloc_)) {}

  Owner(Owner&& other) noexcept
      : alloc_(std::move(other.alloc_)),
        obj_(std::exchange(other.obj_, nullptr)) {}

  // The resources of `other` are taken with std::exchange rather than
  // std::move, which the rvalue-reference-param-not-moved check does not
  // recognise.
  // NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved)
  Owner(std::allocator_arg_t, const Alloc& a,
        Owner&& other) noexcept(always_equal)
      : alloc_(a) {
    // NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved)
    if (always_equal || alloc_ == other.alloc_) {
      obj_ = std::exchange(other.obj_, nullptr);
    } else {
      obj_ = move(other.obj_, alloc_);
      destroy(other.obj_, other.alloc_);
      other.obj_ = nullptr;
    }
  }

  Owner& operator=(const Owner& other) {
    if (this != &other) {
      // We split into two cases: propagating and non-propagating.
      if constexpr (pocca) {
        // Use other's allocator, which we are about to take.
        // We do this BEFORE calling destroy() since it is a
        // potentially throwing operation.
        pointer new_obj = copy(other.obj_, other.alloc_);

        // Everything hereafter will not throw.
        destroy(obj_, alloc_);
        obj_ = new_obj;
        alloc_ = other.alloc_;
      } else {
        // Here, we just need to allocate before destroy().
        pointer new_obj = copy(other.obj_, alloc_);
        destroy(obj_, alloc_);
        obj_ = new_obj;
      }
    }
    return *this;
  }

  // NOLINTBEGIN(bugprone-exception-escape): conditionally noexcept, modelled
  // on allocator-aware standard containers; the potentially throwing copy
  // path is taken only when the noexcept condition is false.
  Owner& operator=(Owner&& other) noexcept(always_equal || pocma) {
    // NOLINTEND(bugprone-exception-escape)
    if (this == &other) {
      return *this;
    }

    // Fast path.
    if (always_equal || pocma || alloc_ == other.alloc_) {
      // Since there is no allocation here, it's fine to do in any order.
      destroy(obj_, alloc_);
      obj_ = std::exchange(other.obj_, nullptr);

      // Still propagate when POCMA is true.
      if constexpr (pocma) {
        alloc_ = other.alloc_;
      }
    } else {  // Slow path.
      // The allocating line - potentially throws.
      pointer new_obj = move(other.obj_, alloc_);
      // After we know the allocation was safe, we destroy the existing
      // object.
      destroy(obj_, alloc_);
      // Destroy the moved-from object.
      destroy(other.obj_, other.alloc_);

      // Commit the results.
      obj_ = new_obj;
      other.obj_ = nullptr;
    }

    return *this;
  }

  void swap(Owner& other) noexcept(always_equal || pocs) {
    if constexpr (!always_equal && !pocs) {
      assert(alloc_ == other.alloc_);
    }

    using std::swap;
    if constexpr (pocs) {
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

using constructors::TestAlloc;

// An allocator that throws on allocation when told to. Exercises
// exception safety without needing a throwing T.
template <typename T>
class ThrowingAlloc : public TestAlloc<T> {
 public:
  const bool* should_throw;

  using TestAlloc<T>::TestAlloc;

  T* allocate(std::size_t count) {
    if (*should_throw) {
      throw std::bad_alloc{};
    }
    return TestAlloc<T>::allocate(count);
  }
};

TEST(TutorialsAllocators, StrongGuarantee) {
  using ThrowingOwner = Owner<int, ThrowingAlloc<int>>;
  bool should_throw = false;

  // Create two objects with unequal allocators. They both
  // point at the same "should I throw?" flag.
  ThrowingAlloc<int> alloc1{1};
  alloc1.should_throw = &should_throw;
  ThrowingOwner o1{std::allocator_arg, alloc1, 10};

  ThrowingAlloc<int> alloc2{2};
  alloc2.should_throw = &should_throw;
  ThrowingOwner o2{std::allocator_arg, alloc2, 20};

  should_throw = true;

  // Copy assignment must allocate to copy o2's value, which throws.
  EXPECT_THROW(o1 = o2, std::bad_alloc);

  // o1 and o2 are left exactly as they were before the assignment.
  EXPECT_EQ(o1.get(), 10);
  EXPECT_EQ(o1.get_allocator().tag, 1);

  EXPECT_EQ(o2.get(), 20);
  EXPECT_EQ(o2.get_allocator().tag, 2);

  // The same applies to move assignment.
  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // tutorial demonstrates the moved-from state on purpose.
  EXPECT_THROW(o1 = std::move(o2), std::bad_alloc);

  EXPECT_EQ(o1.get(), 10);
  EXPECT_EQ(o2.get(), 20);
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
}

}  // namespace xyz::tutorials::strong_guarantee

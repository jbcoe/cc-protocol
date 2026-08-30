// Tests for member function forwarding in the C++26-reflection-based
// implementation of protocol and protocol_view.

#include <gtest/gtest.h>

#include <cstddef>
#include <format>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "protocol.hh"

using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// Counts copies and moves so tests can assert how arguments and return
// values travel through the vtable.
struct CopyCounter {
  static inline int copies = 0;
  static inline int moves = 0;
  int value = 0;

  CopyCounter() = default;

  explicit CopyCounter(int initial_value) : value(initial_value) {}

  CopyCounter(const CopyCounter& other) : value(other.value) { ++copies; }

  CopyCounter(CopyCounter&& other) noexcept : value(other.value) { ++moves; }

  CopyCounter& operator=(const CopyCounter&) = default;
  CopyCounter& operator=(CopyCounter&&) = default;

  static void reset() {
    copies = 0;
    moves = 0;
  }
};

// ---------------------------------------------------------------------------
// Argument forwarding.
// ---------------------------------------------------------------------------

// A moved-from std::unique_ptr is specified to be null, unlike the
// valid-but-unspecified state of a moved-from standard container.
TEST(ReflectionProtocolTest, RvalueReferenceParameterIsMovedFrom) {
  struct Interface {
    void take(std::unique_ptr<int>&&);
    int taken() const;
  };

  struct Conforming {
    int taken_value = 0;

    void take(std::unique_ptr<int>&& pointer) {
      std::unique_ptr<int> owned = std::move(pointer);
      taken_value = *owned;
    }

    int taken() const { return taken_value; }
  };

  protocol<Interface> p(Conforming{});

  std::unique_ptr<int> source = std::make_unique<int>(5);
  p.take(std::move(source));

  EXPECT_EQ(source, nullptr);
  EXPECT_EQ(p.taken(), 5);
}

TEST(ReflectionProtocolViewTest, RvalueReferenceParameterIsMovedFrom) {
  struct Interface {
    void take(std::unique_ptr<int>&&);
  };

  struct Conforming {
    int taken_value = 0;

    void take(std::unique_ptr<int>&& pointer) {
      std::unique_ptr<int> owned = std::move(pointer);
      taken_value = *owned;
    }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  std::unique_ptr<int> source = std::make_unique<int>(5);
  view.take(std::move(source));

  EXPECT_EQ(source, nullptr);
  EXPECT_EQ(implementation.taken_value, 5);
}

TEST(ReflectionProtocolTest, MoveOnlyByValueParameter) {
  struct Interface {
    int take(std::unique_ptr<int>);
  };

  struct Conforming {
    int take(std::unique_ptr<int> pointer) { return *pointer; }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_EQ(p.take(std::make_unique<int>(5)), 5);
}

TEST(ReflectionProtocolViewTest, MoveOnlyByValueParameter) {
  struct Interface {
    int take(std::unique_ptr<int>);
  };

  struct Conforming {
    int take(std::unique_ptr<int> pointer) { return *pointer; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_EQ(view.take(std::make_unique<int>(5)), 5);
}

TEST(ReflectionProtocolTest, LvalueReferenceParameterMutatesCallerObject) {
  struct Interface {
    void append(std::vector<int>&);
  };

  struct Conforming {
    void append(std::vector<int>& values) { values.push_back(42); }
  };

  protocol<Interface> p(Conforming{});

  std::vector<int> values;
  p.append(values);

  EXPECT_EQ(values, std::vector<int>{42});
}

TEST(ReflectionProtocolViewTest, LvalueReferenceParameterMutatesCallerObject) {
  struct Interface {
    void append(std::vector<int>&);
  };

  struct Conforming {
    void append(std::vector<int>& values) { values.push_back(42); }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  std::vector<int> values;
  view.append(values);

  EXPECT_EQ(values, std::vector<int>{42});
}

TEST(ReflectionProtocolTest, ConstReferenceParameterIsNotCopied) {
  struct Interface {
    int inspect(const CopyCounter&) const;
  };

  struct Conforming {
    int inspect(const CopyCounter& counter) const { return counter.value; }
  };

  protocol<Interface> p(Conforming{});

  CopyCounter::reset();
  CopyCounter counter(9);
  EXPECT_EQ(p.inspect(counter), 9);
  EXPECT_EQ(CopyCounter::copies, 0);
  EXPECT_EQ(CopyCounter::moves, 0);
}

TEST(ReflectionProtocolViewTest, ConstReferenceParameterIsNotCopied) {
  struct Interface {
    int inspect(const CopyCounter&) const;
  };

  struct Conforming {
    int inspect(const CopyCounter& counter) const { return counter.value; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  CopyCounter::reset();
  CopyCounter counter(9);
  EXPECT_EQ(view.inspect(counter), 9);
  EXPECT_EQ(CopyCounter::copies, 0);
  EXPECT_EQ(CopyCounter::moves, 0);
}

TEST(ReflectionProtocolTest, ByValueParameterFromLvalueIsCopiedOnce) {
  struct Interface {
    int consume(CopyCounter);
  };

  struct Conforming {
    int consume(CopyCounter counter) { return counter.value; }
  };

  protocol<Interface> p(Conforming{});

  CopyCounter::reset();
  CopyCounter counter(9);
  EXPECT_EQ(p.consume(counter), 9);
  EXPECT_EQ(CopyCounter::copies, 1);
}

TEST(ReflectionProtocolViewTest, ByValueParameterFromLvalueIsCopiedOnce) {
  struct Interface {
    int consume(CopyCounter);
  };

  struct Conforming {
    int consume(CopyCounter counter) { return counter.value; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  CopyCounter::reset();
  CopyCounter counter(9);
  EXPECT_EQ(view.consume(counter), 9);
  EXPECT_EQ(CopyCounter::copies, 1);
}

TEST(ReflectionProtocolTest, ByValueParameterFromRvalueIsNotCopied) {
  struct Interface {
    int consume(CopyCounter);
  };

  struct Conforming {
    int consume(CopyCounter counter) { return counter.value; }
  };

  protocol<Interface> p(Conforming{});

  CopyCounter::reset();
  EXPECT_EQ(p.consume(CopyCounter{1}), 1);
  EXPECT_EQ(CopyCounter::copies, 0);
}

TEST(ReflectionProtocolViewTest, ByValueParameterFromRvalueIsNotCopied) {
  struct Interface {
    int consume(CopyCounter);
  };

  struct Conforming {
    int consume(CopyCounter counter) { return counter.value; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  CopyCounter::reset();
  EXPECT_EQ(view.consume(CopyCounter{1}), 1);
  EXPECT_EQ(CopyCounter::copies, 0);
}

// The generated wrappers take the interface's declared parameter types
// rather than deducing them, so a braced-init-list and a user-defined
// conversion are accepted as they would be by a direct call. A
// perfect-forwarding wrapper (`template <typename... Ts> operator()(Ts&&...)`)
// could not deduce a type for `{1, 2, 3}`.
TEST(ReflectionProtocolTest, ArgumentsConvertAsForADirectCall) {
  struct Interface {
    std::size_t length(std::string_view) const;
    int sum(const std::vector<int>&) const;
  };

  struct Conforming {
    std::size_t length(std::string_view text) const { return text.size(); }

    int sum(const std::vector<int>& values) const {
      return std::accumulate(values.begin(), values.end(), 0);
    }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_EQ(p.length("four"), 4u);
  EXPECT_EQ(p.sum({1, 2, 3}), 6);
}

TEST(ReflectionProtocolViewTest, ArgumentsConvertAsForADirectCall) {
  struct Interface {
    std::size_t length(std::string_view) const;
    int sum(const std::vector<int>&) const;
  };

  struct Conforming {
    std::size_t length(std::string_view text) const { return text.size(); }

    int sum(const std::vector<int>& values) const {
      return std::accumulate(values.begin(), values.end(), 0);
    }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_EQ(view.length("four"), 4u);
  EXPECT_EQ(view.sum({1, 2, 3}), 6);
}

TEST(ReflectionProtocolTest, ManyParametersOfMixedTypes) {
  struct Interface {
    std::string describe(int, double, char, const std::string&, bool,
                         long) const;
  };

  struct Conforming {
    std::string describe(int integer, double real, char character,
                         const std::string& text, bool flag, long big) const {
      return std::format("{},{},{},{},{},{}", integer, real, character, text,
                         flag, big);
    }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_EQ(p.describe(1, 2.5, 'a', "text", true, 7L), "1,2.5,a,text,true,7");
}

TEST(ReflectionProtocolViewTest, ManyParametersOfMixedTypes) {
  struct Interface {
    std::string describe(int, double, char, const std::string&, bool,
                         long) const;
  };

  struct Conforming {
    std::string describe(int integer, double real, char character,
                         const std::string& text, bool flag, long big) const {
      return std::format("{},{},{},{},{},{}", integer, real, character, text,
                         flag, big);
    }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_EQ(view.describe(1, 2.5, 'a', "text", true, 7L),
            "1,2.5,a,text,true,7");
}

// ---------------------------------------------------------------------------
// Return value forwarding.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, LvalueReferenceReturnAliasesStoredObject) {
  struct Interface {
    int& counter();
  };

  struct Conforming {
    int count = 0;

    int& counter() { return count; }
  };

  protocol<Interface> p(Conforming{});

  p.counter() += 5;

  EXPECT_EQ(p.counter(), 5);
  EXPECT_EQ(&p.counter(), &p.counter());
}

TEST(ReflectionProtocolViewTest, LvalueReferenceReturnAliasesStoredObject) {
  struct Interface {
    int& counter();
  };

  struct Conforming {
    int count = 0;

    int& counter() { return count; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  view.counter() += 5;

  EXPECT_EQ(view.counter(), 5);
  EXPECT_EQ(&view.counter(), &view.counter());
  EXPECT_EQ(&view.counter(), &implementation.count);
}

TEST(ReflectionProtocolTest, ConstReferenceReturnIsNotCopied) {
  struct Interface {
    const std::string& name() const;
  };

  struct Conforming {
    std::string name_value = "conforming";

    const std::string& name() const { return name_value; }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_EQ(&p.name(), &p.name());
  EXPECT_EQ(p.name(), "conforming");
}

TEST(ReflectionProtocolViewTest, ConstReferenceReturnIsNotCopied) {
  struct Interface {
    const std::string& name() const;
  };

  struct Conforming {
    std::string name_value = "conforming";

    const std::string& name() const { return name_value; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_EQ(&view.name(), &view.name());
  EXPECT_EQ(view.name(), "conforming");
}

TEST(ReflectionProtocolTest, MoveOnlyReturnValue) {
  struct Interface {
    std::unique_ptr<int> release();
  };

  struct Conforming {
    std::unique_ptr<int> release() { return std::make_unique<int>(7); }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_EQ(*p.release(), 7);
}

TEST(ReflectionProtocolViewTest, MoveOnlyReturnValue) {
  struct Interface {
    std::unique_ptr<int> release();
  };

  struct Conforming {
    std::unique_ptr<int> release() { return std::make_unique<int>(7); }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_EQ(*view.release(), 7);
}

TEST(ReflectionProtocolTest, ByValueReturnIsNotCopied) {
  struct Interface {
    CopyCounter make() const;
  };

  struct Conforming {
    CopyCounter make() const { return CopyCounter{3}; }
  };

  protocol<Interface> p(Conforming{});

  CopyCounter::reset();
  CopyCounter result = p.make();
  EXPECT_EQ(CopyCounter::copies, 0);
  EXPECT_EQ(result.value, 3);
}

TEST(ReflectionProtocolViewTest, ByValueReturnIsNotCopied) {
  struct Interface {
    CopyCounter make() const;
  };

  struct Conforming {
    CopyCounter make() const { return CopyCounter{3}; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  CopyCounter::reset();
  CopyCounter result = view.make();
  EXPECT_EQ(CopyCounter::copies, 0);
  EXPECT_EQ(result.value, 3);
}

TEST(ReflectionProtocolTest, VoidReturn) {
  struct Interface {
    void bump();
    int count() const;
  };

  struct Conforming {
    int value = 0;

    void bump() { ++value; }

    int count() const { return value; }
  };

  protocol<Interface> p(Conforming{});

  p.bump();
  p.bump();

  EXPECT_EQ(p.count(), 2);
}

TEST(ReflectionProtocolViewTest, VoidReturn) {
  struct Interface {
    void bump();
    int count() const;
  };

  struct Conforming {
    int value = 0;

    void bump() { ++value; }

    int count() const { return value; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  view.bump();
  view.bump();

  EXPECT_EQ(view.count(), 2);
}

// ---------------------------------------------------------------------------
// Exceptions.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, ExceptionPropagatesThroughThunk) {
  struct Interface {
    void fail();
    int count() const;
  };

  struct Conforming {
    int value = 42;

    void fail() { throw std::runtime_error("fail"); }

    int count() const { return value; }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_THROW(p.fail(), std::runtime_error);

  try {
    p.fail();
    FAIL() << "expected std::runtime_error to be thrown";
  } catch (const std::runtime_error& error) {
    EXPECT_EQ(std::string(error.what()), "fail");
  }

  EXPECT_EQ(p.count(), 42);
}

TEST(ReflectionProtocolViewTest, ExceptionPropagatesThroughThunk) {
  struct Interface {
    void fail();
    int count() const;
  };

  struct Conforming {
    int value = 42;

    void fail() { throw std::runtime_error("fail"); }

    int count() const { return value; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_THROW(view.fail(), std::runtime_error);

  try {
    view.fail();
    FAIL() << "expected std::runtime_error to be thrown";
  } catch (const std::runtime_error& error) {
    EXPECT_EQ(std::string(error.what()), "fail");
  }

  EXPECT_EQ(view.count(), 42);
}

// ---------------------------------------------------------------------------
// Object identity.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, ImplementationSeesItsOwnThis) {
  struct Interface {
    const void* self() const;
  };

  struct Conforming {
    const void* self() const { return this; }
  };

  protocol<Interface> p(Conforming{});

  EXPECT_EQ(p.self(), p.self());

  protocol<Interface> copy(p);
  EXPECT_NE(copy.self(), p.self());
}

TEST(ReflectionProtocolViewTest, ImplementationSeesItsOwnThis) {
  struct Interface {
    const void* self() const;
  };

  struct Conforming {
    const void* self() const { return this; }
  };

  Conforming implementation;
  protocol_view<Interface> view(implementation);

  EXPECT_EQ(view.self(), &implementation);
}

}  // namespace

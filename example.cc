// For a given struct, protocol_STRUCT should enable type erasure and a
// structurally compatible interface.

struct A {
  void foo() const;
  int bar();
};

// Ideally we'd write `xyz::protocol<A>` and the code below would be generated
// using reflection. For now, we manually write protocol_A. Next we'll write
// some AST-based tooling to generate protocol_A.
//
// `A` must be a complete type for reflection/AST inspection to be able to
// generate our interface.

// BEGIN Generated code for protocol_A
namespace xyz {
class protocol_A {
  class control_block {
    // TODO: Maybe use a manual vtable for speed rather than virtual functions.
   public:
    // Special functions for ANY control block, named to avoid collisions.
    virtual control_block* xyz_protocol_clone(/* SOME ARGS */) = 0;
    virtual control_block* xyz_protocol_move(/* SOME ARGS */) = 0;
    virtual void xyz_protocol_destroy(/* SOME ARGS */) = 0;

    // BEGIN Structurally compatible interface.
   public:
    virtual void foo() const = 0;
    virtual int bar() = 0;
    // END Structurally compatible interface.
  };

  template <typename T>
  class direct_control_block final : public control_block {
    union uninitialized_storage {
      T t_;

      constexpr uninitialized_storage() {}

      constexpr ~uninitialized_storage() {}
    } storage_;

   public:
    override control_block* xyz_protocol_clone(/* SOME ARGS */) {
      // TODO: Implement cloning. Base this on polymorphic.
    }

    override control_block* xyz_protocol_move(/* SOME ARGS */) {
      // TODO: Implement move. Base this on polymorphic.
    }

    override void xyz_protocol_destroy(/* SOME ARGS */) {
      // TODO: Implement cloning. Base this on polymorphic.
    }

    // BEGIN Structurally compatible interface.
   public:
    override void foo() const { return storage_.t_.foo(); }

    override int bar() { return storage_.t_.bar(); }

    // END Structurally compatible interface.
  };

  control_block* p_;

 public:
  template <typename... Us>
  protocol_A(Us&&...);

  // Constructors/destructors can be largely copied from polymorphic.
  // https://eel.is/c++draft/polymorphic

  constexpr bool valueless_after_move() const noexcept { return p_ == nullptr; }

  // BEGIN Structurally compatible interface.
 public:
  void foo() const { return p_->foo(); }

  int bar() { return p_->bar(); }

  // END Structurally compatible interface.
};
}  // namespace xyz

// END Generated code for protocol_A

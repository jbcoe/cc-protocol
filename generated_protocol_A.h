// BEGIN Generated code for protocol_A

namespace xyz {

template <typename T>
concept xyz_protocol_concept_A = requires(T& t) {

  
  { std::as_const(t).name() } -> std::same_as<std::string_view>;
  

  
  { t.count() } -> std::same_as<int>;
  

};

template <typename Allocator = std::allocator<std::byte>>
class protocol_A {
  class control_block {
   public:
    virtual control_block* xyz_protocol_clone(const Allocator& alloc) = 0;
    virtual control_block* xyz_protocol_move(const Allocator& alloc) = 0;
    virtual void xyz_protocol_destroy(const Allocator& alloc) = 0;

   public:

    virtual std::string_view name() const = 0;

    virtual int count() = 0;

  };

  template <typename T>
  class direct_control_block final : public control_block {
    union uninitialized_storage {
      T t_;
      constexpr uninitialized_storage() {}
      constexpr ~uninitialized_storage() {}
    } storage_;

    using cb_allocator = typename std::allocator_traits<
        Allocator>::template rebind_alloc<direct_control_block<T>>;
    using cb_alloc_traits = std::allocator_traits<cb_allocator>;

   public:
    template <class... Ts>
    constexpr direct_control_block(const Allocator& alloc, Ts&&... ts) {
      cb_allocator cb_alloc(alloc);
      cb_alloc_traits::construct(cb_alloc, std::addressof(storage_.t_),
                                 std::forward<Ts>(ts)...);
    }

    control_block* xyz_protocol_clone(const Allocator& alloc) {
      cb_allocator cb_alloc(alloc);
      auto mem = cb_alloc_traits::allocate(cb_alloc, 1);
      try {
        cb_alloc_traits::construct(cb_alloc, mem, alloc, storage_.t_);
        return mem;
      } catch (...) {
        cb_alloc_traits::deallocate(cb_alloc, mem, 1);
        throw;
      }
    }

    control_block* xyz_protocol_move(const Allocator& alloc) {
      cb_allocator cb_alloc(alloc);
      auto mem = cb_alloc_traits::allocate(cb_alloc, 1);
      try {
        cb_alloc_traits::construct(cb_alloc, mem, alloc,
                                   std::move(storage_.t_));
        return mem;
      } catch (...) {
        cb_alloc_traits::deallocate(cb_alloc, mem, 1);
        throw;
      }
    }

    void xyz_protocol_destroy(const Allocator& alloc) {
      cb_allocator cb_alloc(alloc);
      cb_alloc_traits::destroy(cb_alloc, std::addressof(storage_.t_));
      cb_alloc_traits::deallocate(cb_alloc, this, 1);
    }

   public:

    std::string_view name() const { return storage_.t_.name(); }

    int count() { return storage_.t_.count(); }

  };

  using allocator_traits = std::allocator_traits<Allocator>;

  template <class U, class... Ts>
  [[nodiscard]] constexpr control_block* create_control_block(
      Ts&&... ts) const {
    using cb_allocator = typename std::allocator_traits<
        Allocator>::template rebind_alloc<direct_control_block<U>>;
    cb_allocator cb_alloc(alloc_);
    using cb_alloc_traits = std::allocator_traits<cb_allocator>;
    auto mem = cb_alloc_traits::allocate(cb_alloc, 1);
    try {
      cb_alloc_traits::construct(cb_alloc, mem, alloc_,
                                 std::forward<Ts>(ts)...);
      return mem;
    } catch (...) {
      cb_alloc_traits::deallocate(cb_alloc, mem, 1);
      throw;
    }
  }

  control_block* cb_;
  [[no_unique_address]] Allocator alloc_;

 public:
  explicit constexpr protocol_A()
    requires std::default_initializable<A> && xyz_protocol_concept_A<A> &&
             std::default_initializable<A> && std::copy_constructible<A>
      : protocol_A(std::allocator_arg_t{}, A{}) {}

  template <class U>
  constexpr explicit protocol_A(U&& u)
    requires(!std::same_as<protocol_A, std::remove_cvref_t<U>>) &&
            std::copy_constructible<std::remove_cvref_t<U>> &&
            std::default_initializable<A> && xyz_protocol_concept_A<U>
      : protocol_A(std::allocator_arg_t{}, A{}, std::forward<U>(u)) {}

  template <class U, class... Ts>
  explicit constexpr protocol_A(std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> &&
             std::default_initializable<Allocator> && xyz_protocol_concept_A<U>
      : protocol_A(std::allocator_arg_t{}, Allocator{}, std::in_place_type<U>,
                   std::forward<Ts>(ts)...) {}

  template <class U, class I, class... Ts>
  explicit constexpr protocol_A(std::in_place_type_t<U>,
                                std::initializer_list<I> ilist, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             std::constructible_from<U, std::initializer_list<I>, Ts&&...> &&
             std::copy_constructible<U> &&
             std::default_initializable<Allocator> && xyz_protocol_concept_A<U>
      : protocol_A(std::allocator_arg_t{}, Allocator{}, std::in_place_type<U>,
                   ilist, std::forward<Ts>(ts)...) {}

  constexpr protocol_A(const protocol_A& other)
      : protocol_A(std::allocator_arg_t{},
                   allocator_traits::select_on_container_copy_construction(
                       other.alloc_),
                   other) {}

  constexpr protocol_A(protocol_A&& other) noexcept(
      allocator_traits::is_always_equal::value)
      : protocol_A(std::allocator_arg_t{}, other.alloc_, std::move(other)) {}

  explicit constexpr protocol_A(std::allocator_arg_t, const Allocator& alloc)
    requires std::default_initializable<A> && std::copy_constructible<A>
      : alloc_(alloc) {
    cb_ = create_control_block<A>();
  }

  template <class U>
  constexpr explicit protocol_A(std::allocator_arg_t, const Allocator& alloc,
                                U&& u)
    requires(not std::same_as<protocol_A, std::remove_cvref_t<U>>) &&
            std::copy_constructible<std::remove_cvref_t<U>> &&
            xyz_protocol_concept_A<U>
      : alloc_(alloc) {
    cb_ = create_control_block<std::remove_cvref_t<U>>(std::forward<U>(u));
  }

  template <class U, class... Ts>
  explicit constexpr protocol_A(std::allocator_arg_t, const Allocator& alloc,
                                std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> && xyz_protocol_concept_A<U>
      : alloc_(alloc) {
    cb_ = create_control_block<U>(std::forward<Ts>(ts)...);
  }

  template <class U, class I, class... Ts>
  explicit constexpr protocol_A(std::allocator_arg_t, const Allocator& alloc,
                                std::in_place_type_t<U>,
                                std::initializer_list<I> ilist, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             std::constructible_from<U, std::initializer_list<I>, Ts&&...> &&
             std::copy_constructible<U> && xyz_protocol_concept_A<U>
      : alloc_(alloc) {
    cb_ = create_control_block<U>(ilist, std::forward<Ts>(ts)...);
  }

  constexpr protocol_A(std::allocator_arg_t, const Allocator& alloc,
                       const protocol_A& other)
      : alloc_(alloc) {
    if (!other.valueless_after_move()) {
      cb_ = other.cb_->xyz_protocol_clone(alloc_);
    } else {
      cb_ = nullptr;
    }
  }

  constexpr protocol_A(
      std::allocator_arg_t, const Allocator& alloc,
      protocol_A&& other) noexcept(allocator_traits::is_always_equal::value)
      : alloc_(alloc) {
    if constexpr (allocator_traits::is_always_equal::value) {
      cb_ = std::exchange(other.cb_, nullptr);
    } else {
      if (alloc_ == other.alloc_) {
        cb_ = std::exchange(other.cb_, nullptr);
      } else {
        if (!other.valueless_after_move()) {
          cb_ = other.cb_->xyz_protocol_move(alloc_);
        } else {
          cb_ = nullptr;
        }
      }
    }
  }

  constexpr bool valueless_after_move() const noexcept {
    return cb_ == nullptr;
  }

 public:

  std::string_view name() const { return cb_->name(); }

  int count() { return cb_->count(); }


};

}  // namespace xyz

// END Generated code for protocol_A
// Thoughts on how to use current and future reflection features for protocol.

#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace lifetime {

struct allocating_storage {
  void *ptr{nullptr};  // to make it default initializable

  constexpr allocating_storage() {}

  template <typename T>
  constexpr allocating_storage(T &&obj)
      : ptr{new std::remove_cvref_t<T>(std::forward<T>(obj))} {}

  template <typename T>
  constexpr void destroy_object(this allocating_storage &self) {
    delete static_cast<T *>(self.ptr);
  }

  template <typename T>
  constexpr auto &get_object(this auto &&self) {
    return std::forward_like<decltype(self)>(*static_cast<T *>(self.ptr));
  }
};

struct reference_storage {
  void *ptr;

  static reference_storage capture_object(auto &obj) {
    return std::addressof(obj);
  }

  template <typename T>
  constexpr auto &get_object(this auto &&self) {
    return *static_cast<T *>(self.ptr);
  }
};

template <size_t N, size_t Alignment>
struct short_buffer_storage {
  alignas(Alignment) std::array<char, N> data;

  short_buffer_storage(auto &&obj) : data{} {
    std::construct_at<std::remove_cvref_t<decltype(obj)>>(
        data.data(), std::forward<decltype(obj)>(obj));
  }

  static short_buffer_storage capture_object(auto &&obj) {
    return short_buffer_storage{std::forward<decltype(obj)>(obj)};
  }

  template <typename T>
  constexpr void release_object(this auto &&self) {
    delete std::start_lifetime_as<std::remove_reference_t<decltype(self)>>(
        self.data.data());
  }

  template <typename T>
  constexpr auto &get_object(this auto &&self) {
    return std::forward_like<decltype(self)>(
        *std::start_lifetime_as<std::remove_reference_t<decltype(self)>>(
            self.data.data()));
  }
};

}  // namespace lifetime

template <typename Source, typename Lifetime>
struct protocol_builder {
  struct wrapper;
  struct vtable_type;

  consteval {
    auto member_functions =
        members_of(^^Source) | std::views::filter(std::meta::is_function);

    // VTABLE
    auto vtable_members =
        wrapper_member_functions | transform([](std::meta::info mf) {
          auto params = parameters_of(mf);
          // this:
          const auto This = params[0];  // original this
          auto type = ^^Lifetime;
          if (is_const(This)) {
            type = add_const(type);
          }

          if (is_lvalue_reference_type(This)) {
            type = add_lvalue_reference(type);
          } else if (is_rvalue_reference_type(This)) {
            type = add_rvalue_reference(type);
          }

          params[0] = type;  // new `this`, but as first argument
          auto fptr_type = MAKE_FUNCTION_POINTER(
              {.return_type = return_type_of(mf),
               .parameters = params,
               .noexcept = is_noexcept(mf)}) return data_member_spec{
              .type = fptr_type, .name = identifier_of(mf)};
        }) |
        std::ranges::to<std::vector>;

    auto vtable = define_aggregate(^^vtable_type, vtable_members);

    // TODO: add copy / move / assign/ destroy support

    // TODO assignments needs special handling
    auto wrapper_member_functions =
        wrapper_member_functions | transform([](std::meta::info mf) {
          auto params = parameters_of(mf);
          // this:
          const auto This = params[0];  // original this
          auto type = ^^wrapper;
          if (is_const(This)) {
            type = add_const(type);
          }

          if (is_lvalue_reference_type(This)) {
            type = add_lvalue_reference(type);
          } else if (is_rvalue_reference_type(This)) {
            type = add_rvalue_reference(type);
          }

          params[0] = type;  // new `this`
          return MEMBER_FUNCTION_SPEC{.return_type = return_type_of(mf),
                                      .name = identifier_of(mf),
                                      .parameters = params};
        }) |
        std::ranges::to<std::vector>;

    // wrapper will contain:
    // pointer vtable + storage
    std::vector<std::meta::info> wrapper_nonstatic_data_members{
        data_member_spec(add_pointer(add_const(vtable)), {.name = "__vtable"})};

    if (is_default_constructible_type(^^Source)) {
      wrapper_member_functions.push_back(CONSTRUCTOR_SPEC{});
      wrapper_nonstatic_data_members.push_back(data_member_spec(
          ^^Lifetime, {
                          .name = "__storage", .DEFAULTED = true}));
    } else {
      wrapper_nonstatic_data_members.push_back(data_member_spec(
          ^^Lifetime, {
                          .name = "__storage", .DEFAULTED = false}));
    }

    if (is_copy_constructible_type(^^Source)) {
      wrapper_member_functions.push_back(
          CONSTRUCTOR_SPEC{.type = add_lvalue_reference(add_const(^^Wrapper))});
    }

    if (is_copy_constructible_type(^^Source)) {
      wrapper_member_functions.push_back(
          CONSTRUCTOR_SPEC{.type = add_rvalue_reference(^^Wrapper)});
    }

    if (HAS_MEMBER_FUNCTION_TEMPLATE(^^Lifetime, "release_object")) {
      wrapper_member_functions.push_back(DESTRUCTOR_SPEC{});
    }

    wrapper_member_functions.push_back(CONSTRUCTOR_SPEC{
        template constructor taking any compatible object with Source});

    // define class, but not only declares members!
    auto wrp = DEFINE_CLASS(^^wrapper, wrapper_member_functions,
                            wrapper_nonstatic_data_members);

    for (const auto member :
         member_of(wrp) | std::views::filter(std::meta::is_function)) {
      if (is_default_constructor(member)) {
        // nothing
      } else if (is_copy_constructor(member)) {
        DEFINE_CONSTRUCTOR(member, copy vtable pointer,
                           and call Lifetime.copy_object());
      } else if (is_move_constructor(member)) {
        DEFINE_CONSTRUCTOR(member, copy vtable pointer,
                           and call Lifetime.move_object());
      } else if (is_constructor(member)) {
        DEFINE_TEMPLATE_CONSTRUCTOR(
            member, pass auto &&object to storage,
            and assign vtable pointer to specialization);
      } else {
        // API from Source
      }
    }
  }
};

struct animal {
  void make_a_sound(float loudness) const;
};

template <typename T, typename Lifetime>
struct basic_protocol;

template <>
struct basic_protocol<animal, lifetime::allocating_storage> {
  using source_type = animal;
  using lifetime_type = lifetime::allocating_storage;

  lifetime_type __storage;

  struct __vtable_type {
    void (*__destroy_self)(lifetime_type &) = nullptr;
    lifetime_type (*__copy_self)(const lifetime_type &) = nullptr;
    lifetime_type (*__move_self)(lifetime_type &&) = nullptr;
    void (*make_a_sound)(const lifetime_type &, float loudness) = nullptr;
  };

  template <typename T>
  static constexpr auto __vtable_implementation = __vtable_type{
      .__destroy_self =
          +[](lifetime_type &obj) -> void { obj.destroy_object<T>(); },
      .__copy_self = +[](const lifetime_type &obj) -> lifetime_type {
        return lifetime_type{obj.get_object<T>()};
      },
      .__move_self = +[](lifetime_type &&obj) -> lifetime_type {
        return lifetime_type{obj.get_object<T>()};
      },
      .make_a_sound = +[](const lifetime_type &obj, float loudness) -> void {
        obj.get_object<T>().make_a_sound(loudness);
      }};

  const __vtable_type *__vtable{nullptr};

  [[gnu::used]] basic_protocol() /*requires
                                    (std::default_initializable<source_type>)*/
      = default;

  template <typename T>
  basic_protocol(T &&object)
    requires(!std::same_as<basic_protocol, std::remove_cvref_t<T>>)
      : __storage{std::forward<T>(object)},
        __vtable{&__vtable_implementation<std::remove_cvref_t<T>>} {
    // constructs object in storage and sets the vtable
  }

  [[gnu::used]] basic_protocol(const basic_protocol &other)
      : __storage{other.__vtable->__copy_self(other.__storage)},
        __vtable{other.__vtable} {
    // asks the vtable to provide a copy of the storage
  }

  [[gnu::used]] basic_protocol(basic_protocol &&other)
      : __storage{other.__vtable->__move_self(std::move(other.__storage))},
        __vtable{other.__vtable} {
    // asks the vtable to provide a movecopy of the storage
  }

  [[gnu::used]] ~basic_protocol() { __vtable->__destroy_self(__storage); }

  [[gnu::used]] void make_a_sound(float loudness) {
    return __vtable->make_a_sound(__storage, loudness);
  }
};

template <typename Source>
using protocol = basic_protocol<Source, lifetime::allocating_storage>;

#include <cstdio>

struct dog {
  void make_a_sound(float loudness) const { puts("bark"); }
};

protocol<animal> convert(dog &&d) { return {std::move(d)}; }

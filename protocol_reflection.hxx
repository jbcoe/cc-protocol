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
// The first end-to-end vertical slice of the C++26-reflection backend: a
// single xyz::protocol<T, Allocator> class template body that works for any
// interface satisfying reflection_protocol_concept, with no per-interface
// code generation step.
//
// Deliberately narrow: default-allocator construction (using the real
// Allocator template parameter, but none of the allocator-extended
// constructor overloads yet), copy/move/destroy, and dispatch, one
// interface at a time with no narrowing conversions between
// protocol<T,...> specializations. xyz::protocol_view is out of scope
// here; it is added once dispatch is proven.
#ifndef XYZ_PROTOCOL_REFLECTION_HXX_
#define XYZ_PROTOCOL_REFLECTION_HXX_

#include <memory>
#include <meta>
#include <utility>
#include <vector>

#include "protocol_reflection_detail/conformance.hxx"
#include "protocol_reflection_detail/forwarders.hxx"
#include "protocol_reflection_detail/members.hxx"
#include "protocol_reflection_detail/naming.hxx"
#include "protocol_reflection_detail/thunk.hxx"
#include "protocol_reflection_detail/types.hxx"
#include "protocol_reflection_detail/vtable_layout.hxx"

namespace xyz {
namespace reflection_detail {

// The data member of `struct_type` named `name`, as produced by
// naming.hxx's escaping: for splicing a value into a reflection-built
// vtable's entry, once the entry's own name (not just its position) is
// the only thing identifying it.
consteval std::meta::info find_data_member(std::meta::info struct_type,
                                           std::string_view name) {
  for (std::meta::info member : std::meta::nonstatic_data_members_of(
           struct_type, std::meta::access_context::current())) {
    if (std::meta::identifier_of(member) == name) return member;
  }
  throw std::meta::exception("data member not found", ^^void);
}

// A per-interface-member forwarder, giving protocol<Interface, Allocator>
// ordinary call syntax for that member with no splicing at any call site.
// operator() is a member function template because its return type and
// arguments vary per Member, which a single fixed declaration can't
// express; unlike conformance.hxx's candidate forwarders, there is exactly
// one operator() per Wrapper here, so a generic template creates no
// overload-resolution ambiguity to worry about.
template <typename Interface, typename Allocator, std::meta::info Member>
struct protocol_member_wrapper {
  template <typename... Args>
  auto operator()(Args&&... args) const
      noexcept(std::meta::is_noexcept(Member));
};

template <typename Interface, typename Allocator>
consteval std::meta::info protocol_bases_type() {
  std::vector<std::meta::info> bases;
  for (std::meta::info member : interface_member_functions(^^Interface)) {
    std::meta::info wrapper_type = std::meta::substitute(
        ^^protocol_member_wrapper,
        {
            ^^Interface, ^^Allocator, std::meta::reflect_constant(member)});
    bases.push_back(std::meta::substitute(
        ^^forwarder_base,
        {
            wrapper_type, std::meta::reflect_constant(member)}));
  }
  return forwarders_type(bases);
}

// protocol<Interface, Allocator>'s own base list: one forwarder_base per
// interface member, combined via forwarders.hxx's combinator. protocol
// inherits this directly (not through an intermediate type) so that each
// Wrapper's base-to-derived static_cast to protocol<Interface, Allocator>
// (defined out of line below, once protocol is complete) is valid.
template <typename Interface, typename Allocator>
using protocol_bases = typename[:protocol_bases_type<Interface, Allocator>():];

}  // namespace reflection_detail

template <typename T, typename Allocator>
class protocol : public reflection_detail::protocol_bases<T, Allocator> {
  template <typename, typename, std::meta::info>
  friend struct reflection_detail::protocol_member_wrapper;

  using clone_or_move_fn = void* (*)(void*, const Allocator&);
  using destroy_fn = void (*)(void*, const Allocator&);

  // protocol's own vtable: clone/move/destroy, plus one entry per
  // interface member, all erased through void* (never const void*).
  // The owning object itself provides both const and non-const access
  // paths, so a single erased pointer kind suffices here, unlike
  // vtable_layout.hxx's view_vtable/const_view_vtable split.
  struct vtable_incomplete;
  consteval {
    std::vector<std::meta::info> specs = {
        std::meta::data_member_spec(^^clone_or_move_fn,
                                    {
                                        .name = "xyz_protocol_clone"}),
        std::meta::data_member_spec(^^clone_or_move_fn,
                                    {
                                        .name = "xyz_protocol_move"}),
        std::meta::data_member_spec(^^destroy_fn,
                                    {
                                        .name = "xyz_protocol_destroy"}),
    };
    for (std::meta::info spec :
         reflection_detail::define_vtable_entries(^^T, ^^void*, false)) {
      specs.push_back(spec);
    }
    define_aggregate(^^vtable_incomplete, specs);
  }

 public:
  using vtable = vtable_incomplete;

 private:
  template <typename Implementation>
  struct vtable_impl {
    using t_allocator = typename std::allocator_traits<
        Allocator>::template rebind_alloc<Implementation>;
    using t_alloc_traits = std::allocator_traits<t_allocator>;

    static void* xyz_protocol_clone(void* cb, const Allocator& alloc) {
      auto* self = static_cast<Implementation*>(cb);
      t_allocator t_alloc(alloc);
      auto mem = t_alloc_traits::allocate(t_alloc, 1);
      try {
        t_alloc_traits::construct(t_alloc, mem, *self);
        return mem;
      } catch (...) {
        t_alloc_traits::deallocate(t_alloc, mem, 1);
        throw;
      }
    }

    static void* xyz_protocol_move(void* cb, const Allocator& alloc) {
      auto* self = static_cast<Implementation*>(cb);
      t_allocator t_alloc(alloc);
      auto mem = t_alloc_traits::allocate(t_alloc, 1);
      try {
        t_alloc_traits::construct(t_alloc, mem, std::move(*self));
        return mem;
      } catch (...) {
        t_alloc_traits::deallocate(t_alloc, mem, 1);
        throw;
      }
    }

    static void xyz_protocol_destroy(void* cb, const Allocator& alloc) {
      auto* self = static_cast<Implementation*>(cb);
      t_allocator t_alloc(alloc);
      t_alloc_traits::destroy(t_alloc, self);
      t_alloc_traits::deallocate(t_alloc, self, 1);
    }

    static consteval vtable build() {
      vtable vt{};
      vt.xyz_protocol_clone = xyz_protocol_clone;
      vt.xyz_protocol_move = xyz_protocol_move;
      vt.xyz_protocol_destroy = xyz_protocol_destroy;
      template for (constexpr std::meta::info member : std::define_static_array(
                        reflection_detail::interface_member_functions(^^T))) {
        constexpr std::meta::info entry = reflection_detail::find_data_member(
            ^^vtable, reflection_detail::vtable_entry_name(member));
        constexpr std::meta::info merged_type =
            reflection_detail::candidate_overload_set_type(
                reflection_detail::resolve_implementation_candidates(
                    member, ^^Implementation),
                ^^Implementation&);
        using Thunk = reflection_detail::erased_call_thunk<
            Implementation,
            typename[:merged_type:], typename
                      [:reflection_detail::member_function_type(
                            member):], false, std::meta::is_noexcept(member)>;
        vt.[:entry:] = &Thunk::call;
      }
      return vt;
    }

    static constexpr vtable vtable_ = build();
  };

  template <typename U, typename... Ts>
  [[nodiscard]] void* create_storage(Ts&&... ts) const {
    using t_allocator =
        typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
    t_allocator t_alloc(alloc_);
    using t_alloc_traits = std::allocator_traits<t_allocator>;
    auto mem = t_alloc_traits::allocate(t_alloc, 1);
    try {
      t_alloc_traits::construct(t_alloc, mem, std::forward<Ts>(ts)...);
      return mem;
    } catch (...) {
      t_alloc_traits::deallocate(t_alloc, mem, 1);
      throw;
    }
  }

  void* p_;
  const vtable* vtable_;
  [[no_unique_address]] Allocator alloc_;

 public:
  template <class U, class... Ts>
  explicit constexpr protocol(std::in_place_type_t<U>, Ts&&... ts)
    requires std::same_as<std::remove_cvref_t<U>, U> &&
             not_protocol_or_view<U> && std::constructible_from<U, Ts&&...> &&
             std::copy_constructible<U> &&
             std::default_initializable<Allocator> &&
             reflection_detail::reflection_protocol_concept<U, T>
      : alloc_() {
    p_ = create_storage<U>(std::forward<Ts>(ts)...);
    vtable_ = &vtable_impl<U>::vtable_;
  }

  constexpr protocol(const protocol& other) : alloc_(other.alloc_) {
    if (!other.valueless_after_move()) {
      p_ = other.vtable_->xyz_protocol_clone(other.p_, alloc_);
      vtable_ = other.vtable_;
    } else {
      p_ = nullptr;
      vtable_ = nullptr;
    }
  }

  constexpr protocol(protocol&& other) noexcept : alloc_(other.alloc_) {
    p_ = std::exchange(other.p_, nullptr);
    vtable_ = std::exchange(other.vtable_, nullptr);
  }

  constexpr bool valueless_after_move() const noexcept { return p_ == nullptr; }

  ~protocol() {
    if (p_ != nullptr) {
      vtable_->xyz_protocol_destroy(p_, alloc_);
    }
  }

  protocol& operator=(protocol other) noexcept {
    std::swap(p_, other.p_);
    std::swap(vtable_, other.vtable_);
    std::swap(alloc_, other.alloc_);
    return *this;
  }

  void swap(protocol& other) noexcept {
    std::swap(p_, other.p_);
    std::swap(vtable_, other.vtable_);
    std::swap(alloc_, other.alloc_);
  }

  friend void swap(protocol& lhs, protocol& rhs) noexcept { lhs.swap(rhs); }
};

namespace reflection_detail {

template <typename Interface, typename Allocator, std::meta::info Member>
template <typename... Args>
auto protocol_member_wrapper<Interface, Allocator, Member>::operator()(
    Args&&... args) const noexcept(std::meta::is_noexcept(Member)) {
  using Owner = protocol<Interface, Allocator>;
  using Base = forwarder_base<protocol_member_wrapper, Member>;
  const auto* base = static_cast<const Base*>(static_cast<const void*>(this));
  const auto* owner = static_cast<const Owner*>(base);
  constexpr std::meta::info entry =
      find_data_member(^^typename Owner::vtable, vtable_entry_name(Member));
  return owner->vtable_->[:entry:](owner->p_, std::forward<Args>(args)...);
}

}  // namespace reflection_detail

}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_HXX_

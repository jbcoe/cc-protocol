// Tests for xyz::detail::generate_vtable_specs: a vtable entry's
// function-pointer type must carry the interface member's noexcept-ness
// (issue #365), since the view trampolines in vtable.hh deduce their own
// noexcept-ness from that pointer type.

#include <gtest/gtest.h>

#include <meta>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "name_mangling.hh"
#include "vtable.hh"

namespace {

struct NoexceptInterface {
  int f(int x) const noexcept;
};

struct ThrowingInterface {
  int f(int x) const;
};

using xyz::detail::vtable_t;

// The vtable entry for `Member`, found by its mangled name rather than
// xyz::detail::find_vtable_entry: that helper looks the name up through
// xyz::detail::mangled_name_of, a variable template backed by
// std::define_static_string, and this GCC trunk snapshot emits a duplicate
// symbol when that template is instantiated for `Member` a second time
// (once here, once inside generate_vtable_specs). Calling
// xyz::name_mangling::mangle directly avoids the shared template.
template <std::meta::info VtableType, std::meta::info Member>
consteval std::meta::info vtable_entry_for() {
  std::string name = xyz::name_mangling::mangle(Member);
  std::vector<std::meta::info> members = nonstatic_data_members_of(
      VtableType, std::meta::access_context::unprivileged());
  for (std::meta::info member : members) {
    if (identifier_of(member) == name) return member;
  }
  throw std::runtime_error("vtable entry not found");
}

TEST(VtableTest, NoexceptInterfaceMemberGetsANoexceptVtableEntry) {
  using EntryType =
      typename[:type_of(vtable_entry_for<^^vtable_t<NoexceptInterface>,
                                         ^^NoexceptInterface::f>()):];
  static_assert(std::is_nothrow_invocable_v<EntryType, const void*, int>);
}

TEST(VtableTest, ThrowingInterfaceMemberGetsANonNoexceptVtableEntry) {
  using EntryType =
      typename[:type_of(vtable_entry_for<^^vtable_t<ThrowingInterface>,
                                         ^^ThrowingInterface::f>()):];
  static_assert(!std::is_nothrow_invocable_v<EntryType, const void*, int>);
}

}  // namespace

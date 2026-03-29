#ifndef XYZ_PROTOCOL_INTERFACE_A_H
#define XYZ_PROTOCOL_INTERFACE_A_H
#include <string_view>

namespace xyz {

struct A {
  std::string_view name() const noexcept;
  int count();
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_A_H

#ifndef XYZ_PROTOCOL_INTERFACE_A_SUBSET_H
#define XYZ_PROTOCOL_INTERFACE_A_SUBSET_H
#include <string_view>

namespace xyz {

struct A_Subset {
  std::string_view name() const noexcept;
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_A_SUBSET_H

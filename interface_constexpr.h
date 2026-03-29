#ifndef XYZ_PROTOCOL_INTERFACE_CONSTEXPR_H
#define XYZ_PROTOCOL_INTERFACE_CONSTEXPR_H
#include <string_view>

namespace xyz {

struct ConstexprInterface {
  constexpr std::string_view name() const noexcept;
  constexpr int count();
};

struct ConstexprInterface_manual {
  constexpr std::string_view name() const noexcept;
  constexpr int count();
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_CONSTEXPR_H

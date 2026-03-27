#ifndef XYZ_PROTOCOL_INTERFACE_BENCHMARK_H
#define XYZ_PROTOCOL_INTERFACE_BENCHMARK_H
#include <string_view>

namespace xyz {

struct A_virtual {
  std::string_view name() const { return "A_virtual"; }

  int count() { return 42; }
};

struct A_manual {
  std::string_view name() const { return "A_manual"; }

  int count() { return 42; }
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_BENCHMARK_H

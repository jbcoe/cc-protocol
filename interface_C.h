#ifndef XYZ_PROTOCOL_INTERFACE_C_H
#define XYZ_PROTOCOL_INTERFACE_C_H
#include <string>

namespace xyz {

struct C {
  int compute(int x);
  double compute(double x);
  std::string compute(const std::string& x) const;
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_C_H

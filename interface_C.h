#pragma once
#include <string>

namespace xyz {

struct C {
  int compute(int x);
  double compute(double x);
  std::string compute(const std::string& x) const;
};

}  // namespace xyz
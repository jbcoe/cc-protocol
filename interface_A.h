#pragma once
#include <string_view>

namespace xyz {

struct A {
  std::string_view name() const;
  int count();
};

}  // namespace xyz
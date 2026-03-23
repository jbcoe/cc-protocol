#pragma once
#include <string>
#include <vector>

namespace xyz {

struct B {
  void process(const std::string& input);
  std::vector<int> get_results() const;
  bool is_ready() const;
};

}  // namespace xyz
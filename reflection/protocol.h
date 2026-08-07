// A C++26-reflection-based implementation of protocol and protocol_view.

#include <cstddef>
#include <memory>
#include <type_traits>

namespace xyz::reflection {

template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol {
 public:
  // Special member functions.
  protocol() = delete;

  protocol(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol(protocol&&)
    requires std::is_move_constructible_v<T>;

  protocol& operator=(const protocol&)
    requires std::is_copy_assignable_v<T>;

  protocol& operator=(protocol&&)
    requires std::is_move_assignable_v<T>;

  ~protocol();  // Unconstrained.
};

template <typename T>
class protocol_view {
 public:
  // Special member functions.
  protocol_view() = delete;
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) = default;
  ~protocol_view() = default;
};

}  // namespace xyz::reflection

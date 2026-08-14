#include "protocol.h"
#include "tagged_allocator.h"
#include "tracking_allocator.h"

#include <gtest/gtest.h>

struct Blank {};
struct NonCopyable {
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;

    ~NonCopyable() = default;
};

TEST(ProtocolTest, ConstructionAllocations) {
    unsigned allocs{};
    unsigned deallocs{};
    {
        xyz::TrackingAllocator<std::byte> alloc{&allocs, &deallocs};
        xyz::protocol<Blank, decltype(alloc)> p{std::allocator_arg, alloc, 15};
        EXPECT_EQ(allocs, 1);
        EXPECT_EQ(deallocs, 0);
    }
    EXPECT_EQ(allocs, 1);
    EXPECT_EQ(deallocs, 1);
}


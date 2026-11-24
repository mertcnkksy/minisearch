#include "minisearch/data_structures/hash_table.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <new>
#include <utility>

namespace {

thread_local bool fail_next_array_allocation = false;

class FailNextArrayAllocation {
  public:
    FailNextArrayAllocation() noexcept { fail_next_array_allocation = true; }
    ~FailNextArrayAllocation() { fail_next_array_allocation = false; }

    FailNextArrayAllocation(const FailNextArrayAllocation &) = delete;
    FailNextArrayAllocation &operator=(const FailNextArrayAllocation &) = delete;
};

} // namespace

// This replacement is linked only into the allocation-failure test executable.
void *operator new[](std::size_t bytes) {
    if (std::exchange(fail_next_array_allocation, false)) {
        throw std::bad_alloc{};
    }
    return ::operator new(bytes);
}

void operator delete[](void *address) noexcept { ::operator delete(address); }
void operator delete[](void *address, std::size_t) noexcept { ::operator delete(address); }

namespace {

TEST(HashTableAllocationTest, FailedBucketAllocationPreservesTableAndAllowsRetry) {
    minisearch::HashTable<int, int> table;
    constexpr int live_limit = 6;
    for (int key = 0; key < live_limit; ++key) {
        ASSERT_TRUE(table.insert(key, key * 10));
    }
    ASSERT_EQ(table.capacity(), 8U);
    const auto *original_value = table.find(0);
    ASSERT_NE(original_value, nullptr);

    const auto expect_unchanged = [&] {
        EXPECT_EQ(table.capacity(), 8U);
        EXPECT_EQ(table.size(), static_cast<std::size_t>(live_limit));
        EXPECT_EQ(table.find(0), original_value);
        EXPECT_FALSE(table.contains(live_limit));
        for (int key = 0; key < live_limit; ++key) {
            const auto *value = table.find(key);
            ASSERT_NE(value, nullptr);
            EXPECT_EQ(*value, key * 10);
        }
    };

    EXPECT_THROW(
        {
            const FailNextArrayAllocation failure;
            table.insert(live_limit, 60);
        },
        std::bad_alloc);
    expect_unchanged();

    EXPECT_THROW(
        {
            const FailNextArrayAllocation failure;
            table.reserve(30);
        },
        std::bad_alloc);
    expect_unchanged();

    ASSERT_TRUE(table.insert(live_limit, 60));
    EXPECT_EQ(table.capacity(), 16U);
    EXPECT_EQ(table.size(), 7U);
    EXPECT_TRUE(table.contains(live_limit));
}

} // namespace

#include "minisearch/data_structures/min_heap.hpp"

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

TEST(MinHeapAllocationTest, FailedGrowthAndReservePreserveAliasedTopAndAllowRetry) {
    minisearch::MinHeap<int> heap;
    heap.push(7);
    ASSERT_EQ(heap.capacity(), heap.size());
    const auto *original = &heap.top();

    EXPECT_THROW(
        {
            const FailNextArrayAllocation failure;
            heap.push(heap.top());
        },
        std::bad_alloc);
    EXPECT_EQ(heap.size(), 1U);
    EXPECT_EQ(heap.capacity(), 1U);
    EXPECT_EQ(&heap.top(), original);
    EXPECT_EQ(heap.top(), 7);

    EXPECT_THROW(
        {
            const FailNextArrayAllocation failure;
            heap.reserve(8);
        },
        std::bad_alloc);
    EXPECT_EQ(heap.size(), 1U);
    EXPECT_EQ(heap.capacity(), 1U);
    EXPECT_EQ(&heap.top(), original);
    EXPECT_EQ(heap.top(), 7);

    ASSERT_NO_THROW(heap.push(heap.top()));
    EXPECT_EQ(heap.size(), 2U);
    EXPECT_EQ(heap.top(), 7);
    heap.pop();
    EXPECT_EQ(heap.top(), 7);
}

struct StatefulOrder {
    bool descending;

    bool operator()(int left, int right) const noexcept {
        return descending ? left > right : left < right;
    }
};

TEST(MinHeapAllocationTest, FailedCopyAssignmentKeepsDestinationContentsAndComparator) {
    using Heap = minisearch::MinHeap<int, StatefulOrder>;
    Heap source{StatefulOrder{true}};
    source.push(3);
    source.push(9);
    Heap destination{StatefulOrder{false}};
    destination.reserve(8);
    destination.push(2);
    destination.push(4);
    const auto *original = &destination.top();

    EXPECT_THROW(
        {
            const FailNextArrayAllocation failure;
            destination = source;
        },
        std::bad_alloc);
    EXPECT_EQ(destination.capacity(), 8U);
    EXPECT_EQ(destination.size(), 2U);
    EXPECT_EQ(&destination.top(), original);
    EXPECT_EQ(destination.top(), 2);
    destination.push(1);
    EXPECT_EQ(destination.top(), 1);
    EXPECT_EQ(source.top(), 9);

    ASSERT_NO_THROW(destination = source);
    destination.push(10);
    EXPECT_EQ(destination.top(), 10);
    EXPECT_EQ(source.top(), 9);
}

} // namespace

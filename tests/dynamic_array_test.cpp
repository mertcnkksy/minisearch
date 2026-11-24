#include "minisearch/data_structures/dynamic_array.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <new>
#include <numeric>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using PostingList = minisearch::DynamicArray<std::uint32_t>;

static_assert(std::contiguous_iterator<decltype(std::declval<PostingList &>().begin())>);
static_assert(
    std::same_as<decltype(std::declval<const PostingList &>().begin()), const std::uint32_t *>);
static_assert(std::is_nothrow_move_constructible_v<PostingList>);
static_assert(std::is_nothrow_move_assignable_v<PostingList>);

template <typename T>
concept SupportedElement = requires { typename minisearch::DynamicArray<T>; };

struct NoDefaultConstructor {
    explicit NoDefaultConstructor(int) {}
};

static_assert(!SupportedElement<NoDefaultConstructor>);
static_assert(!SupportedElement<std::vector<int>>);
static_assert(!SupportedElement<const std::uint32_t>);
static_assert(!SupportedElement<volatile std::uint32_t>);

struct alignas(64) FixedPosting {
    const std::uint32_t id = 0;
};

static_assert(std::is_trivially_copyable_v<FixedPosting>);
static_assert(!std::is_copy_assignable_v<FixedPosting>);

struct AllocationControlled {
    static inline bool fail_allocation = false;

    std::uint32_t value = 0;

    static void *operator new[](std::size_t bytes) {
        if (fail_allocation) {
            throw std::bad_alloc{};
        }
        return ::operator new[](bytes);
    }

    static void operator delete[](void *address) noexcept { ::operator delete[](address); }
};

struct ConstructionControlled {
    static inline bool fail_construction = false;

    ConstructionControlled() {
        if (fail_construction) {
            throw std::runtime_error("default construction failed");
        }
    }

    std::uint32_t value = 0;
};

TEST(DynamicArrayTest, DefaultConstructionProducesAnEmptyArray) {
    const PostingList list;

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0U);
    EXPECT_EQ(list.capacity(), 0U);
    EXPECT_EQ(list.begin(), list.end());
    EXPECT_EQ(std::distance(list.begin(), list.end()), 0);
}

TEST(DynamicArrayTest, PushesOneElement) {
    PostingList list;
    list.push_back(42U);

    EXPECT_FALSE(list.empty());
    ASSERT_EQ(list.size(), 1U);
    EXPECT_GE(list.capacity(), 1U);
    EXPECT_EQ(list[0], 42U);
}

TEST(DynamicArrayTest, AppendsThousandsOfElementsInOrder) {
    PostingList list;
    constexpr std::uint32_t count = 4096;
    for (std::uint32_t id = 0; id < count; ++id) {
        list.push_back(id);
    }

    ASSERT_EQ(list.size(), count);
    for (std::uint32_t id = 0; id < count; ++id) {
        ASSERT_EQ(list[id], id);
    }
}

TEST(DynamicArrayTest, GrowsCapacityGeometricallyOnlyWhenFull) {
    PostingList list;
    constexpr std::array<std::size_t, 9> expected{1, 2, 4, 4, 8, 8, 8, 8, 16};

    for (const auto capacity : expected) {
        list.push_back(1U);
        EXPECT_EQ(list.capacity(), capacity);
    }
}

TEST(DynamicArrayTest, ReservesExactCapacityWithoutChangingSizeOrValues) {
    PostingList list;
    list.reserve(7);
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.capacity(), 7U);
    EXPECT_EQ(list.begin(), list.end());

    list.push_back(10U);
    list.push_back(20U);
    list.reserve(31);

    EXPECT_EQ(list.capacity(), 31U);
    EXPECT_TRUE(std::ranges::equal(list, std::array{10U, 20U}));
    const auto *first = list.begin();
    list.reserve(31);
    list.reserve(0);
    EXPECT_EQ(list.capacity(), 31U);
    EXPECT_EQ(list.begin(), first);
}

TEST(DynamicArrayTest, SupportsMutableAndConstElementAccess) {
    PostingList list;
    list.push_back(11U);
    list[0] = 12U;
    EXPECT_EQ(std::as_const(list).at(0), 12U);

    list.at(0) = 13U;
    EXPECT_EQ(std::as_const(list)[0], 13U);
}

TEST(DynamicArrayTest, RejectsOutOfRangeAccessEvenWithSpareCapacity) {
    PostingList list;
    list.reserve(8);
    EXPECT_THROW(list.at(0), std::out_of_range);

    list.push_back(1U);
    EXPECT_THROW(list.at(list.size()), std::out_of_range);
    EXPECT_THROW(std::as_const(list).at(2), std::out_of_range);
    EXPECT_THROW(list.at(std::numeric_limits<std::size_t>::max()), std::out_of_range);
}

TEST(DynamicArrayTest, CopyConstructionCreatesIndependentStorage) {
    PostingList source;
    source.reserve(12);
    source.push_back(4U);
    source.push_back(8U);
    PostingList copy(source);

    ASSERT_EQ(copy.size(), source.size());
    EXPECT_EQ(copy.capacity(), copy.size());
    EXPECT_NE(copy.begin(), source.begin());
    EXPECT_TRUE(std::ranges::equal(copy, source));
    copy[0] = 16U;
    EXPECT_EQ(source[0], 4U);
}

TEST(DynamicArrayTest, CopyAssignmentReplacesExistingStorage) {
    PostingList source;
    source.push_back(7U);
    PostingList destination;
    destination.push_back(90U);
    destination.push_back(91U);

    EXPECT_EQ(&(destination = source), &destination);
    ASSERT_EQ(destination.size(), 1U);
    EXPECT_EQ(destination[0], 7U);
    source[0] = 42U;
    EXPECT_EQ(destination[0], 7U);
}

TEST(DynamicArrayTest, CopiesAnEmptyArrayWithoutItsReservedCapacity) {
    PostingList source;
    source.reserve(8);
    const PostingList copy(source);

    EXPECT_TRUE(copy.empty());
    EXPECT_EQ(copy.capacity(), 0U);
    PostingList destination;
    destination.push_back(1U);
    destination = source;
    EXPECT_TRUE(destination.empty());
    EXPECT_EQ(destination.capacity(), 0U);
}

TEST(DynamicArrayTest, MoveConstructionTransfersStorageAndLeavesSourceReusable) {
    PostingList source;
    source.reserve(12);
    source.push_back(27U);
    const auto *original = source.begin();

    const PostingList moved(std::move(source));

    EXPECT_EQ(moved.capacity(), 12U);
    EXPECT_EQ(moved.begin(), original);
    ASSERT_EQ(moved.size(), 1U);
    EXPECT_EQ(moved[0], 27U);
    EXPECT_TRUE(source.empty());
    // DynamicArray explicitly guarantees zero capacity after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.capacity(), 0U);
    source.push_back(99U);
    EXPECT_NE(source.begin(), moved.begin());
    EXPECT_EQ(source.at(0), 99U);
}

TEST(DynamicArrayTest, MoveAssignmentReplacesExistingStorageAndEmptiesSource) {
    PostingList source;
    source.push_back(14U);
    source.reserve(16);
    const auto *original = source.begin();
    PostingList destination;
    destination.push_back(54U);

    EXPECT_EQ(&(destination = std::move(source)), &destination);
    EXPECT_EQ(destination.capacity(), 16U);
    EXPECT_EQ(destination.begin(), original);
    ASSERT_EQ(destination.size(), 1U);
    EXPECT_EQ(destination[0], 14U);
    EXPECT_TRUE(source.empty());
    // DynamicArray explicitly guarantees zero capacity after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.capacity(), 0U);
}

TEST(DynamicArrayTest, SelfCopyAndSelfMoveKeepTheAllocationAndValues) {
    PostingList list;
    list.reserve(5);
    list.push_back(44U);
    const auto *original = list.begin();
    auto *alias = &list;

    list = *alias;
    EXPECT_EQ(list.begin(), original);
    EXPECT_EQ(list.capacity(), 5U);
    ASSERT_EQ(list.size(), 1U);
    EXPECT_EQ(list[0], 44U);

    list = std::move(*alias);
    EXPECT_EQ(list.begin(), original);
    EXPECT_EQ(list.capacity(), 5U);
    ASSERT_EQ(list.size(), 1U);
    EXPECT_EQ(list[0], 44U);
}

TEST(DynamicArrayTest, ClearRetainsTheAllocationForReuse) {
    PostingList list;
    list.push_back(5U);
    const auto capacity = list.capacity();

    list.clear();
    list.clear();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.capacity(), capacity);
    EXPECT_EQ(list.begin(), list.end());
    list.push_back(6U);
    EXPECT_EQ(list.capacity(), capacity);
    EXPECT_EQ(list.at(0), 6U);
}

TEST(DynamicArrayTest, PopBackIsSafeWhenEmptyAndRetainsCapacity) {
    PostingList list;
    list.pop_back();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.capacity(), 0U);

    list.push_back(8U);
    list.push_back(9U);
    const auto capacity = list.capacity();
    list.pop_back();
    ASSERT_EQ(list.size(), 1U);
    EXPECT_EQ(list[0], 8U);

    list.pop_back();
    list.pop_back();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.capacity(), capacity);
}

TEST(DynamicArrayTest, ProvidesContiguousMutableAndConstIterators) {
    PostingList list;
    list.reserve(32);
    list.push_back(1U);
    list.push_back(2U);
    list.push_back(3U);

    for (auto &value : list) {
        value *= 2;
    }
    const auto &read_only = list;
    EXPECT_EQ(read_only.end() - read_only.begin(), 3);
    EXPECT_EQ(&read_only[1], read_only.begin() + 1);
    EXPECT_TRUE(std::ranges::equal(read_only, std::array{2U, 4U, 6U}));
}

TEST(DynamicArrayTest, AppendsAnAliasedElementWithAndWithoutReallocation) {
    PostingList list;
    list.push_back(23U);
    ASSERT_EQ(list.size(), list.capacity());
    list.push_back(list[0]);
    EXPECT_TRUE(std::ranges::equal(list, std::array{23U, 23U}));

    list.reserve(4);
    list.push_back(list.at(1));
    EXPECT_TRUE(std::ranges::equal(list, std::array{23U, 23U, 23U}));
}

TEST(DynamicArrayTest, StoresOverAlignedElementsWithoutCopyAssignment) {
    minisearch::DynamicArray<FixedPosting> list;
    list.push_back(FixedPosting{17U});
    list.push_back(list[0]);
    const auto copy = list;

    ASSERT_EQ(copy.size(), 2U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(copy.begin()) % alignof(FixedPosting), 0U);
    EXPECT_EQ(copy[0].id, 17U);
    EXPECT_EQ(copy[1].id, 17U);
}

TEST(DynamicArrayTest, StoresALargePostingList) {
    constexpr std::size_t count = 1U << 20;
    std::vector<std::uint32_t> expected(count);
    std::iota(expected.begin(), expected.end(), 0U);

    PostingList list;
    for (const auto value : expected) {
        list.push_back(value);
    }

    EXPECT_EQ(list.size(), count);
    EXPECT_TRUE(std::ranges::equal(list, expected));
}

TEST(DynamicArrayTest, RejectsCapacityThatWouldOverflowByteCount) {
    PostingList list;
    list.push_back(73U);
    const auto *original = list.begin();
    constexpr auto too_large = std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) + 1;

    EXPECT_THROW(list.reserve(too_large), std::length_error);
    EXPECT_EQ(list.begin(), original);
    EXPECT_EQ(list.capacity(), 1U);
    EXPECT_EQ(list.at(0), 73U);
}

TEST(DynamicArrayTest, RejectsCapacityThatWouldOverflowIteratorDifference) {
    minisearch::DynamicArray<std::uint8_t> list;
    constexpr auto too_large =
        static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) + 1;

    EXPECT_THROW(list.reserve(too_large), std::length_error);
    EXPECT_EQ(list.capacity(), 0U);
    EXPECT_TRUE(list.empty());
}

TEST(DynamicArrayTest, PreservesStateWhenAllocationFails) {
    minisearch::DynamicArray<AllocationControlled> list;
    list.push_back(AllocationControlled{11U});
    const auto *original = list.begin();
    minisearch::DynamicArray<AllocationControlled> destination;
    destination.push_back(AllocationControlled{22U});
    const auto *original_destination = destination.begin();

    AllocationControlled::fail_allocation = true;
    EXPECT_THROW(list.reserve(4), std::bad_alloc);
    EXPECT_THROW(list.push_back(AllocationControlled{13U}), std::bad_alloc);
    EXPECT_THROW(destination = list, std::bad_alloc);
    AllocationControlled::fail_allocation = false;

    EXPECT_EQ(list.begin(), original);
    EXPECT_EQ(list.capacity(), 1U);
    EXPECT_EQ(list.size(), 1U);
    EXPECT_EQ(list.at(0).value, 11U);
    EXPECT_EQ(destination.begin(), original_destination);
    EXPECT_EQ(destination.at(0).value, 22U);
}

TEST(DynamicArrayTest, PreservesStateWhenElementDefaultConstructionThrows) {
    minisearch::DynamicArray<ConstructionControlled> list;
    ConstructionControlled element;
    element.value = 7U;
    list.push_back(element);
    const auto *original = list.begin();

    ConstructionControlled::fail_construction = true;
    EXPECT_THROW(list.reserve(8), std::runtime_error);
    ConstructionControlled::fail_construction = false;

    EXPECT_EQ(list.begin(), original);
    EXPECT_EQ(list.capacity(), 1U);
    EXPECT_EQ(list.size(), 1U);
    EXPECT_EQ(list.at(0).value, 7U);
}

TEST(DynamicArrayTest, MatchesStdVectorAcrossSeededRandomOperations) {
    PostingList list;
    std::vector<std::uint32_t> reference;
    std::mt19937 rng(0xC0FFEEU);

    for (int step = 0; step < 20'000; ++step) {
        SCOPED_TRACE(step);
        const auto operation = rng() % 16U;
        if (operation == 0) {
            list.clear();
            reference.clear();
        } else if (operation <= 2) {
            list.pop_back();
            if (!reference.empty()) {
                reference.pop_back();
            }
        } else if (operation == 3) {
            const auto capacity = static_cast<std::size_t>(rng() % 256U);
            list.reserve(capacity);
            reference.reserve(capacity);
            EXPECT_GE(list.capacity(), capacity);
        } else {
            const auto value = static_cast<std::uint32_t>(rng());
            list.push_back(value);
            reference.push_back(value);
        }

        ASSERT_EQ(list.size(), reference.size());
        ASSERT_GE(list.capacity(), list.size());
        ASSERT_EQ(list.empty(), reference.empty());
        ASSERT_TRUE(std::ranges::equal(list, reference));
    }
}

} // namespace

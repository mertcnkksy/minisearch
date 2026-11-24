#include "minisearch/data_structures/min_heap.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace minisearch::detail {

struct MinHeapTestAccess {
    template <typename T, typename Compare>
    static bool has_heap_order(const MinHeap<T, Compare> &heap) {
        for (std::size_t child = 1; child < heap.size(); ++child) {
            const auto parent = (child - 1) / 2;
            if (heap.precedes(heap.data_[child], heap.data_[parent])) {
                return false;
            }
        }
        return true;
    }
};

} // namespace minisearch::detail

namespace {

using IntHeap = minisearch::MinHeap<int>;
using minisearch::detail::MinHeapTestAccess;

struct ThrowingCompare {
    bool operator()(int left, int right) const { return left < right; }
};

struct ThrowingCompareCopy {
    ThrowingCompareCopy(const ThrowingCompareCopy &);
    bool operator()(int left, int right) const noexcept { return left < right; }
};

struct ThrowingCompareAssignment {
    ThrowingCompareAssignment &operator=(const ThrowingCompareAssignment &);
    bool operator()(int left, int right) const noexcept { return left < right; }
};

struct FixedValue {
    const int number = 0;
    auto operator<=>(const FixedValue &) const = default;
};

template <typename T, typename Compare = std::ranges::less>
concept SupportsHeap = requires { typename minisearch::MinHeap<T, Compare>; };

static_assert(SupportsHeap<int>);
static_assert(SupportsHeap<std::uint32_t>);
static_assert(SupportsHeap<double>);
static_assert(!SupportsHeap<std::string>);
static_assert(!SupportsHeap<FixedValue>);
static_assert(!SupportsHeap<int, ThrowingCompare>);
static_assert(!SupportsHeap<int, ThrowingCompareCopy>);
static_assert(!SupportsHeap<int, ThrowingCompareAssignment>);
static_assert(std::is_same_v<decltype(std::declval<IntHeap &>().top()), const int &>);
static_assert(noexcept(std::declval<IntHeap &>().pop()));

struct StatefulOrder {
    bool descending;

    explicit StatefulOrder(bool reverse) noexcept : descending(reverse) {}
    StatefulOrder(const StatefulOrder &) noexcept = default;
    StatefulOrder &operator=(const StatefulOrder &) noexcept = default;
    StatefulOrder(StatefulOrder &) = delete;
    StatefulOrder &operator=(StatefulOrder &) = delete;
    StatefulOrder(StatefulOrder &&) = delete;
    StatefulOrder &operator=(StatefulOrder &&) = delete;

    bool operator()(int left, int right) const noexcept {
        return descending ? left > right : left < right;
    }
};

using StatefulHeap = minisearch::MinHeap<int, StatefulOrder>;
static_assert(!std::is_move_constructible_v<StatefulOrder>);
static_assert(std::is_nothrow_move_constructible_v<StatefulHeap>);
static_assert(std::is_nothrow_move_assignable_v<StatefulHeap>);

struct CountingLess {
    std::size_t *calls;

    bool operator()(int left, int right) const noexcept {
        ++*calls;
        return left < right;
    }
};

struct ControlledSlot {
    static inline bool fail_construction = false;
    int value;

    ControlledSlot() : value(0) {
        if (fail_construction) {
            throw std::runtime_error("slot construction failed");
        }
    }

    explicit ControlledSlot(int initial) noexcept : value(initial) {}
    auto operator<=>(const ControlledSlot &) const = default;
};

template <typename Compare>
void expect_pop_order(minisearch::MinHeap<int, Compare> &heap, std::span<const int> expected) {
    EXPECT_TRUE(MinHeapTestAccess::has_heap_order(heap));
    for (const int value : expected) {
        ASSERT_FALSE(heap.empty());
        EXPECT_EQ(heap.top(), value);
        heap.pop();
        EXPECT_TRUE(MinHeapTestAccess::has_heap_order(heap));
    }
    EXPECT_TRUE(heap.empty());
}

TEST(MinHeapTest, EmptyAndSingleElementOperations) {
    IntHeap heap;
    EXPECT_TRUE(heap.empty());
    EXPECT_EQ(heap.size(), 0U);
    EXPECT_EQ(heap.capacity(), 0U);
    EXPECT_THROW(static_cast<void>(heap.top()), std::out_of_range);
    heap.pop();
    EXPECT_TRUE(heap.empty());

    heap.push(-7);
    EXPECT_EQ(std::as_const(heap).top(), -7);
    EXPECT_EQ(heap.size(), 1U);
    expect_pop_order(heap, std::array{-7});
    EXPECT_EQ(heap.capacity(), 1U);
    heap.pop();
    EXPECT_THROW(static_cast<void>(heap.top()), std::out_of_range);
}

TEST(MinHeapTest, PushAndPopMaintainOrderForAscendingDescendingAndShuffledInputs) {
    std::array<int, 64> ordered{};
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        ordered[index] = static_cast<int>(index / 4) - 8;
    }
    auto descending = ordered;
    std::reverse(descending.begin(), descending.end());
    auto shuffled = ordered;
    std::mt19937 random(0xA65EEDU);
    std::shuffle(shuffled.begin(), shuffled.end(), random);

    for (const auto &input : {ordered, descending, shuffled}) {
        IntHeap heap;
        for (const int value : input) {
            heap.push(value);
            ASSERT_TRUE(MinHeapTestAccess::has_heap_order(heap));
        }
        expect_pop_order(heap, ordered);
    }
}

TEST(MinHeapTest, SiftDownSelectsTheSmallerChildIncludingLeftOnly) {
    for (const auto &values : {std::array{1, 2, 3, 4}, std::array{1, 3, 2, 4}}) {
        IntHeap heap{std::span<const int>{values}};
        expect_pop_order(heap, std::array{1, 2, 3, 4});
    }
}

TEST(MinHeapTest, UsesGeometricCapacityAndClearKeepsStorageForReuse) {
    IntHeap heap;
    for (const auto capacity : {1U, 2U, 4U, 4U, 8U, 8U, 8U, 8U, 16U}) {
        heap.push(2);
        EXPECT_EQ(heap.capacity(), capacity);
    }

    heap.clear();
    EXPECT_TRUE(heap.empty());
    EXPECT_EQ(heap.size(), 0U);
    EXPECT_EQ(heap.capacity(), 16U);
    heap.push(3);
    EXPECT_EQ(heap.capacity(), 16U);
    expect_pop_order(heap, std::array{3});
}

TEST(MinHeapTest, ReservePreservesContentsAndChecksCapacityOverflow) {
    IntHeap heap{std::span<const int>{std::array{4, 1, 7}}};
    ASSERT_EQ(heap.capacity(), 3U);
    const auto *original = &heap.top();
    heap.reserve(2);
    heap.reserve(3);
    EXPECT_EQ(heap.capacity(), 3U);
    EXPECT_EQ(&heap.top(), original);

    EXPECT_THROW(heap.reserve(std::numeric_limits<std::size_t>::max()), std::length_error);
    EXPECT_EQ(&heap.top(), original);
    EXPECT_EQ(heap.capacity(), 3U);
    EXPECT_EQ(heap.size(), 3U);
    ASSERT_TRUE(MinHeapTestAccess::has_heap_order(heap));

    heap.reserve(32);
    EXPECT_EQ(heap.capacity(), 32U);
    expect_pop_order(heap, std::array{1, 4, 7});
}

TEST(MinHeapTest, CanPushItsOwnTopWithAndWithoutGrowth) {
    IntHeap heap;
    heap.push(11);
    ASSERT_EQ(heap.capacity(), heap.size());
    heap.push(heap.top());
    EXPECT_EQ(heap.capacity(), 2U);

    heap.reserve(5);
    const auto *root = &heap.top();
    heap.push(heap.top());
    EXPECT_EQ(&heap.top(), root);
    EXPECT_EQ(heap.capacity(), 5U);
    expect_pop_order(heap, std::array{11, 11, 11});
}

TEST(MinHeapTest, CopiesHaveIndependentStorageAndPreserveComparatorState) {
    StatefulHeap source{StatefulOrder{true}};
    source.reserve(16);
    source.push(6);
    source.push(2);
    StatefulHeap copy(source);
    EXPECT_EQ(copy.capacity(), 2U);
    EXPECT_NE(&copy.top(), &source.top());
    EXPECT_EQ(copy.top(), 6);

    StatefulHeap destination{StatefulOrder{false}};
    destination.push(-5);
    destination = source;
    EXPECT_EQ(destination.capacity(), 2U);
    destination.push(8);
    source.push(4);

    expect_pop_order(destination, std::array{8, 6, 2});
    expect_pop_order(source, std::array{6, 4, 2});
    EXPECT_EQ(copy.top(), 6);
    EXPECT_EQ(copy.size(), 2U);
    copy.push(10);
    expect_pop_order(copy, std::array{10, 6, 2});
}

TEST(MinHeapTest, CopyOfReservedEmptyHeapHasZeroCapacity) {
    IntHeap empty;
    empty.reserve(8);
    const IntHeap copy(empty);
    EXPECT_TRUE(copy.empty());
    EXPECT_EQ(copy.capacity(), 0U);

    IntHeap destination;
    destination.push(5);
    destination = empty;
    EXPECT_TRUE(destination.empty());
    EXPECT_EQ(destination.capacity(), 0U);
}

TEST(MinHeapTest, MovesTransferStorageAndKeepBothComparatorsUsable) {
    StatefulHeap source{StatefulOrder{true}};
    source.reserve(16);
    source.push(6);
    source.push(2);
    const auto *original = &source.top();

    StatefulHeap moved(std::move(source));
    EXPECT_EQ(&moved.top(), original);
    EXPECT_EQ(moved.capacity(), 16U);
    EXPECT_TRUE(source.empty());
    // MinHeap guarantees zero capacity and preserves the comparator after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.capacity(), 0U);
    source.push(1);
    source.push(9);
    expect_pop_order(source, std::array{9, 1});

    StatefulHeap destination{StatefulOrder{false}};
    destination.push(-5);
    destination = std::move(moved);
    EXPECT_EQ(&destination.top(), original);
    EXPECT_EQ(destination.capacity(), 16U);
    destination.push(7);
    expect_pop_order(destination, std::array{7, 6, 2});

    EXPECT_TRUE(moved.empty());
    // The same guarantee applies after move assignment.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(moved.capacity(), 0U);
    moved.push(3);
    moved.push(8);
    expect_pop_order(moved, std::array{8, 3});
}

TEST(MinHeapTest, SelfCopyAndSelfMoveKeepCapacityStorageAndValues) {
    IntHeap heap{std::span<const int>{std::array{8, 3}}};
    heap.reserve(16);
    const auto *original = &heap.top();
    auto *alias = &heap;
    heap = *alias;
    EXPECT_EQ(&heap.top(), original);
    heap = std::move(*alias);
    EXPECT_EQ(&heap.top(), original);
    EXPECT_EQ(heap.capacity(), 16U);
    expect_pop_order(heap, std::array{3, 8});
}

TEST(MinHeapTest, DefaultConstructionFailureLeavesExistingStorageUsable) {
    minisearch::MinHeap<ControlledSlot> heap;
    heap.push(ControlledSlot{5});
    ASSERT_EQ(heap.capacity(), 1U);
    const auto *original = &heap.top();

    ControlledSlot::fail_construction = true;
    EXPECT_THROW(heap.push(ControlledSlot{1}), std::runtime_error);
    EXPECT_THROW(heap.reserve(16), std::runtime_error);
    ControlledSlot::fail_construction = false;

    EXPECT_EQ(&heap.top(), original);
    EXPECT_EQ(heap.top().value, 5);
    EXPECT_EQ(heap.size(), 1U);
    EXPECT_EQ(heap.capacity(), 1U);
    EXPECT_TRUE(MinHeapTestAccess::has_heap_order(heap));
    heap.push(ControlledSlot{1});
    EXPECT_EQ(heap.top().value, 1);
}

TEST(MinHeapTest, BottomUpConstructionHandlesEmptySingleAndMultipleInputOrders) {
    IntHeap empty{std::span<const int>{}};
    expect_pop_order(empty, std::span<const int>{});
    EXPECT_EQ(empty.capacity(), 0U);

    IntHeap single{std::span<const int>{std::array{-6}}};
    expect_pop_order(single, std::array{-6});

    constexpr std::array ascending{-5, -1, 0, 1, 3, 3, 4, 10, 20};
    auto descending = ascending;
    std::reverse(descending.begin(), descending.end());
    auto shuffled = ascending;
    std::mt19937 rng(0xF10D);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    for (const auto &input : {ascending, descending, shuffled}) {
        IntHeap heap{std::span<const int>{input}};
        EXPECT_EQ(heap.capacity(), input.size());
        expect_pop_order(heap, ascending);
    }
}

TEST(MinHeapTest, BottomUpConstructionSupportsReversedStatefulOrdering) {
    constexpr std::array values{2, -4, 6, 6, 0, 11};
    StatefulHeap heap{std::span<const int>{values}, StatefulOrder{true}};
    expect_pop_order(heap, std::array{11, 6, 6, 2, 0, -4});
}

TEST(MinHeapTest, BottomUpConstructionStaysWithinTheLinearComparisonBound) {
    for (const std::size_t count : {0U, 1U, 2U, 3U, 16U, 31U, 32U, 127U, 1024U, 65'535U, 65'536U}) {
        SCOPED_TRACE(count);
        std::vector<int> input(count);
        std::iota(input.rbegin(), input.rend(), 1);
        std::size_t comparisons = 0;
        const minisearch::MinHeap<int, CountingLess> heap{std::span<const int>{input},
                                                          CountingLess{&comparisons}};

        const auto total_height = count - static_cast<std::size_t>(std::popcount(count));
        EXPECT_LE(comparisons, 2 * total_height);
        EXPECT_EQ(heap.size(), count);
        EXPECT_TRUE(MinHeapTestAccess::has_heap_order(heap));
        if (count != 0) {
            EXPECT_EQ(heap.top(), 1);
        }
    }
}

TEST(MinHeapTest, MatchesMinPriorityQueueAcrossTwentyThousandSeededOperations) {
    IntHeap heap;
    using Reference = std::priority_queue<int, std::vector<int>, std::greater<int>>;
    Reference reference;
    std::mt19937 rng(0xB1A4EAU);

    for (int step = 0; step < 20'000; ++step) {
        SCOPED_TRACE(step);
        const auto operation = rng() % 64U;
        if (operation == 0) {
            heap.clear();
            reference = Reference{};
        } else if (operation == 1) {
            heap.reserve(static_cast<std::size_t>(rng() % 512U));
            auto copy = heap;
            auto expected = reference;
            while (!expected.empty()) {
                ASSERT_FALSE(copy.empty());
                ASSERT_EQ(copy.top(), expected.top());
                copy.pop();
                expected.pop();
            }
            ASSERT_TRUE(copy.empty());
        } else if (operation <= 29) {
            heap.pop();
            if (!reference.empty()) {
                reference.pop();
            }
        } else if (operation <= 52) {
            const auto value = static_cast<int>(rng() % 1024U) - 512;
            heap.push(value);
            reference.push(value);
        }

        ASSERT_TRUE(MinHeapTestAccess::has_heap_order(heap));
        ASSERT_EQ(heap.size(), reference.size());
        ASSERT_EQ(heap.empty(), reference.empty());
        if (!reference.empty()) {
            ASSERT_EQ(heap.top(), reference.top());
        }
    }
}

struct DocumentResult {
    std::uint32_t document_id;
    double score;
    bool operator==(const DocumentResult &) const = default;
};

// Results must have non-NaN scores. The lowest-ranked selected result is the root.
struct WorstResultFirst {
    bool operator()(const DocumentResult &left, const DocumentResult &right) const noexcept {
        return left.score < right.score ||
               (left.score == right.score && left.document_id > right.document_id);
    }
};

TEST(MinHeapTest, BoundedHeapSelectsTopKAndUsesSmallerIdsToBreakTies) {
    constexpr std::array documents{DocumentResult{90, 1.0}, DocumentResult{80, 1.0},
                                   DocumentResult{17, 8.0}, DocumentResult{7, 8.0},
                                   DocumentResult{2, 8.0},  DocumentResult{55, 0.0}};
    const auto select = [](std::span<const DocumentResult> input, std::size_t k) {
        const WorstResultFirst worse;
        minisearch::MinHeap<DocumentResult, WorstResultFirst> selected;
        selected.reserve(std::min(k, input.size()));
        for (const auto &document : input) {
            if (selected.size() < k) {
                selected.push(document);
            } else if (k != 0 && worse(selected.top(), document)) {
                selected.pop();
                selected.push(document);
            }
            EXPECT_LE(selected.size(), k);
            EXPECT_TRUE(MinHeapTestAccess::has_heap_order(selected));
        }
        std::vector<DocumentResult> ordered;
        while (!selected.empty()) {
            ordered.push_back(selected.top());
            selected.pop();
        }
        std::reverse(ordered.begin(), ordered.end());
        return ordered;
    };

    EXPECT_TRUE(select({}, 3).empty());
    auto ranked = documents;
    std::sort(ranked.begin(), ranked.end(),
              [](const DocumentResult &left, const DocumentResult &right) {
                  if (left.score == right.score) {
                      return left.document_id < right.document_id;
                  }
                  return left.score > right.score;
              });
    for (const std::size_t k : {0U, 1U, 2U, 4U, 12U}) {
        SCOPED_TRACE(k);
        const auto count = std::min(k, ranked.size());
        const auto expected = std::span{ranked}.first(count);
        EXPECT_TRUE(std::ranges::equal(select(documents, k), expected));
    }
    EXPECT_EQ(select(documents, 2), (std::vector<DocumentResult>{{2, 8.0}, {7, 8.0}}));
}

} // namespace

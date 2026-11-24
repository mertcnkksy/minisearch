#include "minisearch/data_structures/hash_table.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

struct IdentityHash {
    std::size_t operator()(int key) const noexcept { return static_cast<std::size_t>(key); }
};

struct LastBucketHash {
    std::size_t operator()(int) const noexcept { return std::numeric_limits<std::size_t>::max(); }
};

struct ModuloHash {
    int divisor;
    std::size_t operator()(int key) const noexcept {
        return static_cast<std::size_t>(key % divisor);
    }
};

struct ModuloEqual {
    int divisor;
    bool operator()(int first, int second) const noexcept {
        return first % divisor == second % divisor;
    }
};

struct ControlledValue {
    static inline bool fail_copy = false;
    static inline bool fail_move = false;
    int data;

    explicit ControlledValue(int value) noexcept : data(value) {}

    ControlledValue(const ControlledValue &other) : data(other.data) {
        if (fail_copy) {
            throw std::bad_alloc{};
        }
    }

    // The injected exception exercises rollback after a failed insertion.
    // NOLINTNEXTLINE(performance-noexcept-move-constructor)
    ControlledValue(ControlledValue &&other) : data(other.data) {
        if (fail_move) {
            throw std::runtime_error("move failed");
        }
    }
};

using IntTable = minisearch::HashTable<int, int, IdentityHash>;
using ExceptionTable = minisearch::HashTable<int, ControlledValue, IdentityHash>;

TEST(HashTableTest, EmptyTableNeedsNoBucketsAndHandlesMisses) {
    IntTable table;

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(table.size(), 0U);
    EXPECT_EQ(table.capacity(), 0U);
    EXPECT_DOUBLE_EQ(table.load_factor(), 0.0);
    EXPECT_EQ(table.find(0), nullptr);
    EXPECT_FALSE(table.contains(0));
    EXPECT_FALSE(table.erase(0));

    table.clear();
    table.reserve(0);
    EXPECT_EQ(table.capacity(), 0U);
}

TEST(HashTableTest, SupportsStringsAndMutableAndConstLookup) {
    minisearch::HashTable<std::string, std::string> table;

    ASSERT_TRUE(table.insert("alpha", "first"));
    auto *value = table.find("alpha");
    ASSERT_NE(value, nullptr);
    *value = "updated";
    const auto *found = std::as_const(table).find("alpha");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, "updated");
    EXPECT_TRUE(table.contains("alpha"));
    EXPECT_EQ(table.size(), 1U);
}

TEST(HashTableTest, RejectsEquivalentKeysWithCustomPolicies) {
    minisearch::HashTable<int, int, ModuloHash, ModuloEqual> table(ModuloHash{10}, ModuloEqual{10});

    ASSERT_TRUE(table.insert(13, 200));
    EXPECT_FALSE(table.insert(23, 300));
    const auto *found = table.find(33);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 200);
    EXPECT_EQ(table.size(), 1U);
    EXPECT_TRUE(table.erase(43));
    EXPECT_FALSE(table.contains(13));
}

TEST(HashTableTest, CollisionsWrapAroundAndRemainReachableAfterErasure) {
    minisearch::HashTable<int, int, LastBucketHash> table;
    ASSERT_TRUE(table.insert(1, 100));
    ASSERT_TRUE(table.insert(2, 200));
    ASSERT_TRUE(table.insert(3, 300));
    const auto *third = table.find(3);
    ASSERT_NE(third, nullptr);

    EXPECT_TRUE(table.erase(1));
    EXPECT_FALSE(table.erase(1));
    EXPECT_TRUE(table.contains(2));
    EXPECT_EQ(table.find(3), third);
    EXPECT_FALSE(table.contains(4));
    EXPECT_FALSE(table.insert(3, 333));
    EXPECT_EQ(*third, 300);

    ASSERT_TRUE(table.insert(4, 400));
    ASSERT_TRUE(table.insert(1, 111));
    EXPECT_EQ(table.capacity(), 8U);
    EXPECT_EQ(table.find(3), third);
    ASSERT_NE(table.find(1), nullptr);
    EXPECT_EQ(*table.find(1), 111);
}

TEST(HashTableTest, GrowsOnlyWhenAnotherLiveEntryWouldExceedLoadLimit) {
    IntTable table;
    for (int key = 0; key < 6; ++key) {
        ASSERT_TRUE(table.insert(key, key * 2));
    }

    ASSERT_EQ(table.capacity(), 8U);
    EXPECT_DOUBLE_EQ(table.load_factor(), table.max_load_factor());
    EXPECT_FALSE(table.insert(3, 100));
    EXPECT_EQ(table.capacity(), 8U);
    ASSERT_NE(table.find(3), nullptr);
    EXPECT_EQ(*table.find(3), 6);

    ASSERT_TRUE(table.insert(6, *table.find(3)));
    EXPECT_EQ(table.capacity(), 16U);
    EXPECT_EQ(table.size(), 7U);
    EXPECT_DOUBLE_EQ(table.load_factor(), 7.0 / 16.0);
    for (int key = 0; key < 7; ++key) {
        const auto *found = table.find(key);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(*found, key < 6 ? key * 2 : 6);
    }
}

TEST(HashTableTest, ReservesForLiveEntriesAndKeepsPointersOnNoOp) {
    IntTable table;
    table.reserve(24);
    EXPECT_EQ(table.capacity(), 32U);
    EXPECT_TRUE(table.empty());

    for (int key = 0; key < 24; ++key) {
        ASSERT_TRUE(table.insert(key, key));
    }
    const auto *original = table.find(0);
    ASSERT_NE(original, nullptr);
    table.reserve(24);
    table.reserve(1);
    EXPECT_EQ(table.find(0), original);
    EXPECT_EQ(table.capacity(), 32U);

    table.reserve(25);
    EXPECT_EQ(table.capacity(), 64U);
    EXPECT_EQ(table.size(), 24U);
    for (int key = 0; key < 24; ++key) {
        const auto *found = table.find(key);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(*found, key);
    }
}

TEST(HashTableTest, RejectsCapacityOverflowBeforeChangingState) {
    IntTable table;
    ASSERT_TRUE(table.insert(1, 2));
    const auto *original = table.find(1);
    ASSERT_NE(original, nullptr);

    EXPECT_THROW(table.reserve(std::numeric_limits<std::size_t>::max()), std::length_error);
    EXPECT_EQ(table.capacity(), 8U);
    EXPECT_EQ(table.size(), 1U);
    EXPECT_EQ(table.find(1), original);
    EXPECT_EQ(*original, 2);
}

TEST(HashTableTest, RehashesCollidingKeysAfterGrowth) {
    minisearch::HashTable<int, int, LastBucketHash> table;
    constexpr int count = 128;

    for (int key = 0; key < count; ++key) {
        ASSERT_TRUE(table.insert(key, -key));
    }
    for (int key = 1; key < count; key += 2) {
        ASSERT_TRUE(table.erase(key));
    }
    table.reserve(256);
    for (int key = 0; key < count; ++key) {
        const auto *found = table.find(key);
        if (key % 2 == 0) {
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(*found, -key);
        } else {
            EXPECT_EQ(found, nullptr);
        }
    }
    EXPECT_EQ(table.size(), 64U);
}

TEST(HashTableTest, ClearsEntriesAndTombstonesWhileRetainingCapacity) {
    IntTable table;
    ASSERT_TRUE(table.insert(0, 10));
    ASSERT_TRUE(table.insert(8, 80));
    ASSERT_TRUE(table.erase(0));

    table.clear();
    table.clear();
    EXPECT_EQ(table.capacity(), 8U);
    EXPECT_TRUE(table.empty());
    EXPECT_DOUBLE_EQ(table.load_factor(), 0.0);
    EXPECT_FALSE(table.contains(0));
    EXPECT_FALSE(table.contains(8));

    ASSERT_TRUE(table.insert(8, 81));
    ASSERT_NE(table.find(8), nullptr);
    EXPECT_EQ(*table.find(8), 81);
}

TEST(HashTableTest, RebuildsTombstoneHeavyStorageWithoutGrowing) {
    ExceptionTable table;
    for (int key = 0; key < 6; ++key) {
        ASSERT_TRUE(table.insert(key, ControlledValue{key}));
    }
    ASSERT_TRUE(table.erase(0));
    ASSERT_TRUE(table.erase(1));
    ASSERT_TRUE(table.insert(6, ControlledValue{6}));
    ASSERT_EQ(table.size(), 5U);
    const auto *original = table.find(2);
    ASSERT_NE(original, nullptr);

    ControlledValue::fail_copy = true;
    bool reused = false;
    EXPECT_NO_THROW(reused = table.insert(8, ControlledValue{8}));
    EXPECT_TRUE(reused);
    EXPECT_TRUE(table.erase(8));
    // Using the last never-used bucket would exceed the used limit.
    EXPECT_THROW(table.insert(7, ControlledValue{7}), std::bad_alloc);
    ControlledValue::fail_copy = false;
    EXPECT_EQ(table.size(), 5U);
    EXPECT_EQ(table.find(2), original);
    EXPECT_FALSE(table.contains(7));

    ASSERT_TRUE(table.insert(7, ControlledValue{7}));
    EXPECT_EQ(table.capacity(), 8U);
    EXPECT_EQ(table.size(), 6U);
    EXPECT_FALSE(table.contains(0));
    EXPECT_FALSE(table.contains(1));
    for (int key = 2; key <= 7; ++key) {
        const auto *found = table.find(key);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->data, key);
    }
}

TEST(HashTableTest, PreservesExistingEntriesWhenCopyingForGrowthFails) {
    ExceptionTable table;
    for (int key = 0; key < 6; ++key) {
        ASSERT_TRUE(table.insert(key, ControlledValue{key}));
    }
    const auto *original = table.find(0);
    ASSERT_NE(original, nullptr);

    ControlledValue::fail_copy = true;
    EXPECT_THROW(table.reserve(20), std::bad_alloc);
    EXPECT_THROW(table.insert(6, ControlledValue{6}), std::bad_alloc);
    ControlledValue::fail_copy = false;

    EXPECT_EQ(table.capacity(), 8U);
    EXPECT_EQ(table.size(), 6U);
    EXPECT_EQ(table.find(0), original);
    EXPECT_EQ(original->data, 0);
    EXPECT_FALSE(table.contains(6));
}

TEST(HashTableTest, KeepsOldStorageWhenConstructingTheNewEntryFailsAfterRehash) {
    ExceptionTable table;
    for (int key = 0; key < 6; ++key) {
        ASSERT_TRUE(table.insert(key, ControlledValue{key}));
    }
    const auto *original = table.find(0);
    ASSERT_NE(original, nullptr);

    ControlledValue::fail_move = true;
    EXPECT_THROW(table.insert(6, ControlledValue{6}), std::runtime_error);
    ControlledValue::fail_move = false;

    EXPECT_EQ(table.capacity(), 8U);
    EXPECT_EQ(table.size(), 6U);
    EXPECT_EQ(table.find(0), original);
    EXPECT_FALSE(table.contains(6));
    EXPECT_TRUE(table.insert(6, ControlledValue{6}));
    EXPECT_EQ(table.capacity(), 16U);
}

TEST(HashTableTest, FailedTombstoneReusePreservesTheProbeChain) {
    ExceptionTable table;
    ASSERT_TRUE(table.insert(0, ControlledValue{0}));
    ASSERT_TRUE(table.insert(8, ControlledValue{8}));
    ASSERT_TRUE(table.erase(0));

    ControlledValue::fail_move = true;
    EXPECT_THROW(table.insert(16, ControlledValue{16}), std::runtime_error);
    ControlledValue::fail_move = false;

    EXPECT_EQ(table.size(), 1U);
    EXPECT_TRUE(table.contains(8));
    EXPECT_FALSE(table.contains(16));
    EXPECT_TRUE(table.insert(16, ControlledValue{16}));
    EXPECT_EQ(table.capacity(), 8U);
}

TEST(HashTableTest, MatchesUnorderedMapAcrossSeededRandomOperations) {
    IntTable table;
    std::unordered_map<int, int, IdentityHash> reference;
    std::mt19937 rng(0x5EA2C4U);

    for (int step = 0; step < 20'000; ++step) {
        SCOPED_TRACE(step);
        const auto key = static_cast<int>(rng() % 256U);
        const auto operation = rng() % 32U;
        if (operation == 0) {
            table.clear();
            reference.clear();
        } else if (operation == 1) {
            table.reserve(static_cast<std::size_t>(rng() % 384U));
        } else if (operation <= 9) {
            EXPECT_EQ(table.erase(key), reference.erase(key) != 0);
        } else if (operation <= 21) {
            EXPECT_EQ(table.insert(key, step), reference.emplace(key, step).second);
        }

        ASSERT_EQ(table.size(), reference.size());
        EXPECT_LE(table.load_factor(), table.max_load_factor());
        const auto expected = reference.find(key);
        const auto *actual = table.find(key);
        if (expected == reference.end()) {
            EXPECT_EQ(actual, nullptr);
        } else {
            ASSERT_NE(actual, nullptr);
            EXPECT_EQ(*actual, expected->second);
        }

        if (step % 127 == 0) {
            for (const auto &[stored_key, value] : reference) {
                const auto *found = table.find(stored_key);
                ASSERT_NE(found, nullptr);
                EXPECT_EQ(*found, value);
            }
        }
    }
}

} // namespace

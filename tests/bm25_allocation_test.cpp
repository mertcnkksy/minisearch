#include "minisearch/ranking/bm25.hpp"

#include "minisearch/index/inverted_index.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace {

thread_local bool recording = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t array_allocations = 0;
thread_local std::size_t fail_at = 0;

class AllocationProbe {
  public:
    explicit AllocationProbe(
        std::size_t failure_index = std::numeric_limits<std::size_t>::max()) noexcept {
        allocations = 0;
        array_allocations = 0;
        fail_at = failure_index;
        recording = true;
    }

    ~AllocationProbe() { recording = false; }

    AllocationProbe(const AllocationProbe &) = delete;
    AllocationProbe &operator=(const AllocationProbe &) = delete;

    [[nodiscard]] std::size_t count() const noexcept { return allocations; }
    [[nodiscard]] std::size_t array_count() const noexcept { return array_allocations; }
};

void *allocate(std::size_t bytes) {
    if (recording) {
        const auto index = allocations++;
        if (index >= fail_at) {
            throw std::bad_alloc{};
        }
    }
    if (auto *memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        return memory;
    }
    throw std::bad_alloc{};
}

} // namespace

// Replace the ordinary scalar and array families only in this test process.
// Over-aligned allocations retain the runtime's matched allocator and deleter.
void *operator new(std::size_t bytes) { return allocate(bytes); }
void *operator new[](std::size_t bytes) {
    if (recording) {
        ++array_allocations;
    }
    return allocate(bytes);
}

void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
    try {
        return ::operator new(bytes);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

void *operator new[](std::size_t bytes, const std::nothrow_t &) noexcept {
    try {
        return ::operator new[](bytes);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept { ::operator delete[](memory); }
void operator delete(void *memory, const std::nothrow_t &) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, const std::nothrow_t &) noexcept {
    ::operator delete[](memory);
}

namespace {

using minisearch::BM25Scorer;
using minisearch::DocumentId;
using minisearch::DynamicArray;
using minisearch::InvertedIndex;
using minisearch::ScoredDocument;

std::string snapshot(const InvertedIndex &index, const std::array<std::string, 4> &terms) {
    std::ostringstream output;
    output << index.document_count() << ' ' << index.unique_term_count() << ' '
           << index.total_token_count() << ' ' << index.average_document_length() << '\n';
    for (DocumentId id = 0; id < index.document_count(); ++id) {
        output << index.document_length(id) << '\n';
    }
    for (const auto &term : terms) {
        output << term << ' ' << index.document_frequency(term) << ' '
               << index.collection_frequency(term) << '\n';
        for (const auto &posting : index.find_postings(term)) {
            output << posting.document_id << ' ' << index.term_frequency(term, posting.document_id);
            for (const auto position : posting.positions) {
                output << ' ' << position;
            }
            output << '\n';
        }
    }
    return output.str();
}

TEST(BM25AllocationTest, QueryFailuresPreserveTheIndexAndPriorScoresAndAllowRetry) {
    const std::string first(96, 'a');
    const std::string second(96, 'b');
    const std::array<std::string, 4> terms{"common", first, second, "missing"};
    const auto query = first + " common " + second + " " + first + " missing";

    InvertedIndex index;
    for (DocumentId id = 0; id < 40; ++id) {
        ASSERT_EQ(index.add_document("common " + (id % 3 == 0 ? first : second)), id);
    }
    const BM25Scorer scorer;
    const auto original = scorer.score_query(index, first);
    const std::vector<ScoredDocument> original_snapshot(original.begin(), original.end());
    const auto before = snapshot(index, terms);

    DynamicArray<ScoredDocument> expected;
    std::size_t count = 0;
    std::size_t arrays = 0;
    {
        const AllocationProbe probe;
        expected = scorer.score_query(index, query);
        count = probe.count();
        arrays = probe.array_count();
    }
    ASSERT_EQ(expected.size(), 40U);
    // The workload grows and rehashes the row map and grows the result array.
    ASSERT_GT(arrays, 2U);

    for (std::size_t failure = 0; failure < count; ++failure) {
        SCOPED_TRACE(failure);
        bool threw = false;
        std::size_t attempts = 0;
        {
            const AllocationProbe probe(failure);
            try {
                static_cast<void>(scorer.score_query(index, query));
            } catch (const std::bad_alloc &) {
                threw = true;
            }
            attempts = probe.count();
        }
        ASSERT_TRUE(threw);
        EXPECT_EQ(attempts, failure + 1);
        EXPECT_EQ(snapshot(index, terms), before);
        EXPECT_TRUE(std::ranges::equal(original, original_snapshot));
        EXPECT_TRUE(std::ranges::equal(scorer.score_query(index, query), expected));
    }
}

} // namespace

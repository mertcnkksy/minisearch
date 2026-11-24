#include "minisearch/search/search_engine.hpp"

#include "minisearch/index/inverted_index.hpp"
#include "minisearch/text/tokenizer.hpp"
#include "search/top_k.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <sstream>
#include <string>

namespace {

class AllocationProbe;
thread_local AllocationProbe *active_probe = nullptr;

class AllocationProbe {
  public:
    explicit AllocationProbe(std::size_t fail_at = std::numeric_limits<std::size_t>::max()) noexcept
        : fail_at_(fail_at) {
        active_probe = this;
    }

    ~AllocationProbe() { active_probe = nullptr; }

    AllocationProbe(const AllocationProbe &) = delete;
    AllocationProbe &operator=(const AllocationProbe &) = delete;

    void observe(bool is_array) {
        if (is_array) {
            ++arrays_;
        }
        if (count_++ >= fail_at_) {
            throw std::bad_alloc{};
        }
    }

    [[nodiscard]] std::size_t count() const noexcept { return count_; }
    [[nodiscard]] std::size_t arrays() const noexcept { return arrays_; }

  private:
    std::size_t fail_at_;
    std::size_t count_ = 0;
    std::size_t arrays_ = 0;
};

void *allocate(std::size_t bytes, bool is_array) {
    if (active_probe != nullptr) {
        active_probe->observe(is_array);
    }
    if (auto *memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        return memory;
    }
    throw std::bad_alloc{};
}

} // namespace

// Only replace the ordinary allocation families in this executable. Aligned
// allocations and their matching deleters remain provided by the runtime.
void *operator new(std::size_t bytes) { return allocate(bytes, false); }
void *operator new[](std::size_t bytes) { return allocate(bytes, true); }

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

using minisearch::DocumentId;
using minisearch::DynamicArray;
using minisearch::InvertedIndex;
using minisearch::ScoredDocument;
using minisearch::search;
using minisearch::Tokenizer;
using minisearch::detail::select_top_k;

TEST(TopKAllocationTest, ReservesHeapAndOutputOnceAndAllowsRetryAtEitherFailure) {
    std::array<ScoredDocument, 80> candidates{};
    for (std::size_t row = 0; row < candidates.size(); ++row) {
        candidates[row] = {static_cast<DocumentId>(row), static_cast<double>(row)};
    }
    const auto input = candidates;
    constexpr std::size_t k = 3;

    DynamicArray<ScoredDocument> expected;
    std::size_t allocations = 0;
    std::size_t arrays = 0;
    {
        const AllocationProbe probe;
        expected = select_top_k(candidates, k);
        allocations = probe.count();
        arrays = probe.arrays();
    }
    ASSERT_EQ(expected.size(), k);
    EXPECT_EQ(expected[0], candidates.back());
    // Ascending scores trigger a replacement for every candidate after the first k.
    // The two requests are the heap reserve and the output reserve.
    ASSERT_EQ(allocations, 2U);
    ASSERT_EQ(arrays, 2U);

    for (std::size_t failure = 0; failure < allocations; ++failure) {
        SCOPED_TRACE(failure);
        auto destination = expected;
        bool threw = false;
        std::size_t attempts = 0;
        {
            const AllocationProbe probe(failure);
            try {
                destination = select_top_k(candidates, k);
            } catch (const std::bad_alloc &) {
                threw = true;
            }
            attempts = probe.count();
        }
        EXPECT_TRUE(threw);
        EXPECT_EQ(attempts, failure + 1);
        EXPECT_EQ(candidates, input);
        EXPECT_TRUE(std::ranges::equal(destination, expected));
        EXPECT_TRUE(std::ranges::equal(select_top_k(candidates, k), expected));
    }
}

std::string snapshot(const InvertedIndex &index, const std::array<std::string, 4> &terms) {
    std::ostringstream output;
    output << index.document_count() << ' ' << index.unique_term_count() << ' '
           << index.total_token_count() << ' ' << index.average_document_length() << '\n';
    for (std::uint64_t id = 0; id < index.document_count(); ++id) {
        output << index.document_length(static_cast<DocumentId>(id)) << '\n';
    }
    for (const auto &term : terms) {
        output << term << ' ' << index.document_frequency(term) << ' '
               << index.collection_frequency(term) << '\n';
        for (const auto &posting : index.find_postings(term)) {
            output << posting.document_id << ' ' << posting.positions.size();
            for (const auto position : posting.positions) {
                output << ' ' << position;
            }
            output << '\n';
        }
    }
    return output.str();
}

TEST(TopKAllocationTest, SearchFailuresPreserveIndexAndPriorResultsAndAllowRetry) {
    const std::string first(96, 'a');
    const std::string second(96, 'b');
    const std::array<std::string, 4> terms{first, second, "common", "missing"};
    const auto query = first + " common " + second + " " + first + " missing";
    constexpr std::size_t k = 7;

    InvertedIndex index;
    for (DocumentId id = 0; id < 48; ++id) {
        ASSERT_EQ(index.add_document("common " + (id % 3 == 0 ? first : second)), id);
    }
    const auto original = search(index, first, k);
    ASSERT_EQ(original.size(), k);
    std::array<ScoredDocument, k> original_before{};
    std::ranges::copy(original, original_before.begin());
    const auto before = snapshot(index, terms);

    DynamicArray<ScoredDocument> expected;
    std::size_t allocations = 0;
    std::size_t arrays = 0;
    {
        const AllocationProbe probe;
        expected = search(index, query, k);
        allocations = probe.count();
        arrays = probe.arrays();
    }
    ASSERT_EQ(expected.size(), k);
    ASSERT_GT(allocations, arrays);
    ASSERT_GT(arrays, 2U);

    for (std::size_t failure = 0; failure < allocations; ++failure) {
        SCOPED_TRACE(failure);
        auto destination = original;
        bool threw = false;
        std::size_t attempts = 0;
        {
            const AllocationProbe probe(failure);
            try {
                destination = search(index, query, k);
            } catch (const std::bad_alloc &) {
                threw = true;
            }
            attempts = probe.count();
        }
        ASSERT_TRUE(threw);
        EXPECT_EQ(attempts, failure + 1);
        EXPECT_EQ(snapshot(index, terms), before);
        EXPECT_TRUE(std::ranges::equal(destination, original_before));
        EXPECT_TRUE(std::ranges::equal(original, original_before));
        EXPECT_TRUE(std::ranges::equal(search(index, first, k), original_before));
        EXPECT_TRUE(std::ranges::equal(search(index, query, k), expected));
    }
}

TEST(TopKAllocationTest, ZeroLimitAllocatesOnlyForQueryValidation) {
    constexpr std::array candidates{ScoredDocument{42, 1.0}};
    DynamicArray<ScoredDocument> zero_selection;
    DynamicArray<ScoredDocument> empty_selection;
    {
        const AllocationProbe probe(0);
        zero_selection = select_top_k(candidates, 0);
        empty_selection = select_top_k({}, std::numeric_limits<std::size_t>::max());
    }
    EXPECT_TRUE(zero_selection.empty());
    EXPECT_TRUE(empty_selection.empty());

    const std::string term(96, 'a');
    const auto query = term + " " + term + " common";
    InvertedIndex index;
    for (std::size_t id = 0; id < 32; ++id) {
        index.add_document(term + " common");
    }

    std::size_t validation_allocations = 0;
    {
        const AllocationProbe probe;
        static_cast<void>(Tokenizer::tokenize(query));
        validation_allocations = probe.count();
    }
    ASSERT_GT(validation_allocations, 0U);

    DynamicArray<ScoredDocument> result;
    std::size_t array_allocations = 0;
    std::size_t allocations = 0;
    {
        const AllocationProbe probe(validation_allocations);
        result = search(index, query, 0);
        allocations = probe.count();
        array_allocations = probe.arrays();
    }
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(allocations, validation_allocations);
    EXPECT_EQ(array_allocations, 0U);
    EXPECT_EQ(search(index, query, 1).size(), 1U);
}

} // namespace

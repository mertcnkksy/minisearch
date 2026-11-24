#include "minisearch/index/inverted_index.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>
#include <sstream>
#include <string>
#include <string_view>

namespace {

thread_local bool recording = false;
thread_local bool fail_after_first = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t array_allocations = 0;
thread_local std::size_t fail_at = 0;

class AllocationProbe {
  public:
    explicit AllocationProbe(std::size_t failure_index = std::numeric_limits<std::size_t>::max(),
                             bool persistent = false) noexcept {
        allocations = 0;
        array_allocations = 0;
        fail_at = failure_index;
        fail_after_first = persistent;
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
        if (index == fail_at || (fail_after_first && index > fail_at)) {
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

using minisearch::DocumentId;
using minisearch::InvertedIndex;

struct CorpusInput {
    std::string long_existing = std::string(96, 'e');
    std::string long_new_first = std::string(96, 'n');
    std::string long_new_second = std::string(96, 'p');
    std::string long_new_third = std::string(96, 'r');

    [[nodiscard]] std::string target() const {
        return "common common " + long_new_first + " " + long_existing + " " + long_new_second +
               " " + long_new_first + " " + long_new_third;
    }

    [[nodiscard]] std::array<std::string, 10> terms() const {
        return {"common",      "alpha",        "beta",          "gamma",        "delta",
                long_existing, long_new_first, long_new_second, long_new_third, "absent"};
    }
};

void seed(InvertedIndex &index, const CorpusInput &input) {
    EXPECT_EQ(index.add_document("common alpha " + input.long_existing), 0U);
    EXPECT_EQ(index.add_document("common beta"), 1U);
    EXPECT_EQ(index.add_document("common gamma"), 2U);
    EXPECT_EQ(index.add_document("common delta"), 3U);
}

std::string snapshot(const InvertedIndex &index, const std::array<std::string, 10> &terms) {
    std::ostringstream result;
    result << index.document_count() << ' ' << index.unique_term_count() << ' '
           << index.total_token_count() << ' ' << index.average_document_length() << '\n';
    for (DocumentId id = 0; id < index.document_count(); ++id) {
        result << id << " length=" << index.document_length(id) << '\n';
    }
    for (const auto &term : terms) {
        result << term << " df=" << index.document_frequency(term)
               << " cf=" << index.collection_frequency(term) << '\n';
        for (DocumentId id = 0; id < index.document_count(); ++id) {
            result << "  tf[" << id << "]=" << index.term_frequency(term, id) << '\n';
        }
        for (const auto &posting : index.find_postings(term)) {
            result << "  positions[" << posting.document_id << ']';
            for (const auto position : posting.positions) {
                result << ' ' << position;
            }
            result << '\n';
        }
    }
    return result.str();
}

bool fail_insertion(InvertedIndex &index, std::string_view text, std::size_t fail_index,
                    bool persistent) {
    bool threw = false;
    std::size_t attempted = 0;
    {
        const AllocationProbe probe(fail_index, persistent);
        try {
            index.add_document(text);
        } catch (const std::bad_alloc &) {
            threw = true;
        }
        attempted = probe.count();
    }
    EXPECT_EQ(attempted, fail_index + 1) << "Rollback must not attempt another allocation";
    return threw;
}

TEST(InvertedIndexAllocationTest, EveryFailurePreservesSnapshotsAndAllowsTheSameDocumentOnRetry) {
    const CorpusInput input;
    const auto terms = input.terms();
    const auto text = input.target();

    InvertedIndex reference;
    seed(reference, input);
    ASSERT_EQ(reference.document_count(), 4U);
    ASSERT_EQ(reference.unique_term_count(), 6U);
    ASSERT_EQ(reference.document_frequency("common"), 4U);
    const auto before = snapshot(reference, terms);

    DocumentId id = 0;
    std::size_t count = 0;
    {
        const AllocationProbe probe;
        id = reference.add_document(text);
        count = probe.count();
    }
    ASSERT_EQ(id, 4U);
    ASSERT_GT(count, 1U);
    const auto after = snapshot(reference, terms);
    EXPECT_EQ(reference.document_frequency("common"), 5U);
    EXPECT_EQ(reference.collection_frequency("common"), 6U);
    EXPECT_EQ(reference.unique_term_count(), 9U);
    EXPECT_EQ(reference.document_length(4), 7U);

    // The fixture crosses the dictionary's live limit and grows the document and common posting
    // vectors. Long keys keep allocation points in the transactional dictionary insertion phase.
    for (const bool persistent : {false, true}) {
        for (std::size_t failure_index = 0; failure_index < count; ++failure_index) {
            SCOPED_TRACE(::testing::Message()
                         << "allocation=" << failure_index << " persistent=" << persistent);
            InvertedIndex index;
            seed(index, input);
            ASSERT_TRUE(fail_insertion(index, text, failure_index, persistent));
            EXPECT_EQ(snapshot(index, terms), before);
            ASSERT_EQ(index.add_document(text), 4U);
            EXPECT_EQ(snapshot(index, terms), after);
        }
    }
}

TEST(InvertedIndexAllocationTest, ConsecutiveFailuresAfterPartialDictionaryInsertionAllowRetry) {
    const CorpusInput input;
    const auto terms = input.terms();
    const auto text = input.target();

    InvertedIndex reference;
    seed(reference, input);
    const auto before = snapshot(reference, terms);
    std::size_t count = 0;
    {
        const AllocationProbe probe;
        reference.add_document(text);
        count = probe.count();
    }
    ASSERT_GT(count, 1U);

    InvertedIndex index;
    seed(index, input);
    // The last allocation copies a new key after earlier new keys have entered the dictionary.
    ASSERT_TRUE(fail_insertion(index, text, count - 1, true));
    EXPECT_EQ(snapshot(index, terms), before);
    ASSERT_TRUE(fail_insertion(index, text, 0, true));
    EXPECT_EQ(snapshot(index, terms), before);
    ASSERT_EQ(index.add_document(text), 4U);
    EXPECT_EQ(snapshot(index, terms), snapshot(reference, terms));
}

std::size_t measure_insertion(InvertedIndex &index, std::string_view text) {
    const AllocationProbe probe;
    index.add_document(text);
    return probe.count();
}

std::size_t measure_array_allocations(InvertedIndex &index, std::string_view text) {
    const AllocationProbe probe;
    index.add_document(text);
    return probe.array_count();
}

TEST(InvertedIndexAllocationTest, TombstoneCleanupAfterRepeatedRollbackIsSafe) {
    std::array<std::string, 8> keys;
    // Choose one key per initial bucket using the actual hash policy.
    for (unsigned suffix = 0; suffix < 1024; ++suffix) {
        const auto candidate = std::string(96, 'k') + std::to_string(suffix);
        auto &key = keys[std::hash<std::string>{}(candidate) & 7U];
        if (key.empty()) {
            key = candidate;
        }
    }
    ASSERT_TRUE(std::ranges::all_of(keys, [](const auto &key) { return !key.empty(); }));

    const auto first = keys[1] + " " + keys[2] + " " + keys[3] + " " + keys[4] + " " + keys[5];
    const auto second = keys[5] + " " + keys[6] + " " + keys[1];

    InvertedIndex first_reference;
    ASSERT_EQ(first_reference.add_document(keys[0]), 0U);
    const auto first_count = measure_insertion(first_reference, first);
    ASSERT_GT(first_count, 0U);

    InvertedIndex second_reference;
    ASSERT_EQ(second_reference.add_document(keys[0]), 0U);
    ASSERT_TRUE(fail_insertion(second_reference, first, first_count - 1, true));
    const auto second_count = measure_insertion(second_reference, second);
    ASSERT_GT(second_count, 0U);

    InvertedIndex control;
    ASSERT_EQ(control.add_document(keys[0]), 0U);
    ASSERT_TRUE(fail_insertion(control, first, first_count - 1, true));
    const auto ordinary_arrays = measure_array_allocations(control, keys[7]);

    InvertedIndex index;
    ASSERT_EQ(index.add_document(keys[0]), 0U);
    ASSERT_TRUE(fail_insertion(index, first, first_count - 1, true));
    ASSERT_TRUE(fail_insertion(index, second, second_count - 1, true));
    EXPECT_EQ(index.unique_term_count(), 1U);
    EXPECT_EQ(index.document_count(), 1U);
    EXPECT_EQ(index.total_token_count(), 1U);
    for (std::size_t slot = 1; slot < keys.size(); ++slot) {
        EXPECT_TRUE(index.find_postings(keys[slot]).empty());
    }

    // Six tombstones plus the base key occupy seven buckets. Inserting into the
    // eighth needs an extra bucket-array allocation for the same-capacity rehash.
    EXPECT_EQ(measure_array_allocations(index, keys[7]), ordinary_arrays + 1);
    EXPECT_EQ(index.document_count(), 2U);
    EXPECT_EQ(index.unique_term_count(), 2U);
    EXPECT_EQ(index.term_frequency(keys[0], 0), 1U);
    EXPECT_EQ(index.term_frequency(keys[7], 1), 1U);
}

} // namespace

#include "minisearch/index/inverted_index.hpp"

#include "index/index_limits.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace minisearch::detail {

struct InvertedIndexTestAccess {
    static std::optional<std::uint32_t> term_id(const InvertedIndex &index,
                                                const std::string &term) {
        const auto *id = index.dictionary_.find(term);
        return id == nullptr ? std::nullopt : std::optional{*id};
    }

    static std::size_t dictionary_capacity(const InvertedIndex &index) {
        return index.dictionary_.capacity();
    }
};

} // namespace minisearch::detail

namespace {

using minisearch::DocumentId;
using minisearch::InvertedIndex;
using minisearch::Position;
using minisearch::Posting;
using minisearch::detail::InvertedIndexTestAccess;

static_assert(!std::is_copy_constructible_v<InvertedIndex>);
static_assert(!std::is_copy_assignable_v<InvertedIndex>);
static_assert(!std::is_move_constructible_v<InvertedIndex>);
static_assert(!std::is_move_assignable_v<InvertedIndex>);
static_assert(std::is_same_v<decltype(std::declval<const InvertedIndex &>().find_postings("x")),
                             std::span<const Posting>>);

void expect_posting(const Posting &posting, DocumentId id, std::span<const Position> positions) {
    EXPECT_EQ(posting.document_id, id);
    EXPECT_TRUE(std::ranges::equal(posting.positions, positions));
}

TEST(InvertedIndexTest, EmptyIndexAndExactTermLookups) {
    const InvertedIndex index;
    EXPECT_EQ(index.document_count(), 0U);
    EXPECT_EQ(index.unique_term_count(), 0U);
    EXPECT_EQ(index.total_token_count(), 0U);
    EXPECT_DOUBLE_EQ(index.average_document_length(), 0.0);
    for (const std::string term : {"", "absent", "ABC", "two words"}) {
        EXPECT_TRUE(index.find_postings(term).empty());
        EXPECT_EQ(index.document_frequency(term), 0U);
        EXPECT_EQ(index.collection_frequency(term), 0U);
        EXPECT_THROW(static_cast<void>(index.term_frequency(term, 0)), std::out_of_range);
    }
    EXPECT_THROW(static_cast<void>(index.document_length(0)), std::out_of_range);
    EXPECT_EQ(index.unique_term_count(), 0U);
}

TEST(InvertedIndexTest, IndexesBothExampleDocumentsWithExactPositionsAndCorpusStatistics) {
    InvertedIndex index;
    EXPECT_EQ(index.add_document("Python supports data structures."), 0U);
    EXPECT_EQ(index.add_document("Python is fast. Python supports algorithms."), 1U);

    EXPECT_EQ(index.document_count(), 2U);
    EXPECT_EQ(index.unique_term_count(), 7U);
    EXPECT_EQ(index.total_token_count(), 10U);
    EXPECT_EQ(index.document_length(0), 4U);
    EXPECT_EQ(index.document_length(1), 6U);
    EXPECT_DOUBLE_EQ(index.average_document_length(), 5.0);

    const auto python = index.find_postings("python");
    ASSERT_EQ(python.size(), 2U);
    expect_posting(python[0], 0, std::array<Position, 1>{0});
    expect_posting(python[1], 1, std::array<Position, 2>{0, 3});
    EXPECT_EQ(index.document_frequency("python"), 2U);
    EXPECT_EQ(index.collection_frequency("python"), 3U);
    EXPECT_EQ(index.term_frequency("python", 0), 1U);
    EXPECT_EQ(index.term_frequency("python", 1), 2U);

    const auto supports = index.find_postings("supports");
    ASSERT_EQ(supports.size(), 2U);
    expect_posting(supports[0], 0, std::array<Position, 1>{1});
    expect_posting(supports[1], 1, std::array<Position, 1>{4});

    struct SinglePosting {
        const char *term;
        DocumentId document;
        Position position;
    };
    for (const auto &[term, document, position] :
         {SinglePosting{"data", 0, 2}, SinglePosting{"structures", 0, 3}, SinglePosting{"is", 1, 1},
          SinglePosting{"fast", 1, 2}, SinglePosting{"algorithms", 1, 5}}) {
        SCOPED_TRACE(term);
        const auto postings = index.find_postings(term);
        ASSERT_EQ(postings.size(), 1U);
        expect_posting(postings.front(), document, std::array{position});
        EXPECT_EQ(index.document_frequency(term), 1U);
        EXPECT_EQ(index.collection_frequency(term), 1U);
        EXPECT_EQ(index.term_frequency(term, document), 1U);
        EXPECT_EQ(index.term_frequency(term, 1U - document), 0U);
    }

    for (const std::string term : {"", "unknown", "Python", "python supports", "python."}) {
        EXPECT_TRUE(index.find_postings(term).empty());
        EXPECT_EQ(index.document_frequency(term), 0U);
        EXPECT_EQ(index.collection_frequency(term), 0U);
        EXPECT_EQ(index.term_frequency(term, 0), 0U);
    }
    EXPECT_THROW(static_cast<void>(index.term_frequency("unknown", 2)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(index.term_frequency("python", 2)), std::out_of_range);
    EXPECT_EQ(index.unique_term_count(), 7U);
}

TEST(InvertedIndexTest, EmptyDocumentsAndDuplicateContentGetTheirOwnIds) {
    InvertedIndex index;
    EXPECT_EQ(index.add_document(""), 0U);
    EXPECT_EQ(index.add_document("! .\t__"), 1U);
    EXPECT_DOUBLE_EQ(index.average_document_length(), 0.0);
    EXPECT_EQ(index.document_length(0), 0U);
    EXPECT_EQ(index.document_length(1), 0U);
    EXPECT_EQ(index.total_token_count(), 0U);
    EXPECT_EQ(index.unique_term_count(), 0U);

    EXPECT_EQ(index.add_document("42"), 2U);
    EXPECT_EQ(index.add_document("42"), 3U);
    EXPECT_EQ(index.document_count(), 4U);
    EXPECT_EQ(index.total_token_count(), 2U);
    EXPECT_DOUBLE_EQ(index.average_document_length(), 0.5);
    EXPECT_EQ(index.term_frequency("42", 0), 0U);
    const auto postings = index.find_postings("42");
    ASSERT_EQ(postings.size(), 2U);
    expect_posting(postings[0], 2, std::array<Position, 1>{0});
    expect_posting(postings[1], 3, std::array<Position, 1>{0});
}

TEST(InvertedIndexTest, OwnsTermsAndKeepsFirstSeenIdsStableAcrossRehashes) {
    InvertedIndex index;
    std::string text = "ZeBra apple ZEBRA";
    EXPECT_EQ(index.add_document(text), 0U);
    text.assign(1024, '!');
    EXPECT_EQ(index.add_document(std::string("MANGO")), 1U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "zebra"), 0U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "apple"), 1U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "mango"), 2U);
    const auto initial_capacity = InvertedIndexTestAccess::dictionary_capacity(index);

    for (std::uint32_t number = 0; number < 600; ++number) {
        const auto term = "newterm" + std::to_string(number);
        EXPECT_EQ(index.add_document(term), number + 2);
        EXPECT_EQ(InvertedIndexTestAccess::term_id(index, term), number + 3);
    }
    EXPECT_GT(InvertedIndexTestAccess::dictionary_capacity(index), initial_capacity);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "zebra"), 0U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "mango"), 2U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "newterm97"), 100U);
    const auto zebra = index.find_postings("zebra");
    ASSERT_EQ(zebra.size(), 1U);
    expect_posting(zebra.front(), 0, std::array<Position, 2>{0, 2});
}

TEST(InvertedIndexTest, RejectsInvalidAsciiWithoutConsumingDocumentOrTermIds) {
    InvertedIndex index;
    ASSERT_EQ(index.add_document("old old"), 0U);
    EXPECT_THROW(index.add_document("\x80"), std::invalid_argument);
    auto rejected = std::string(65'536, 'A') + " new old ";
    rejected += static_cast<char>(0xFF);
    EXPECT_THROW(index.add_document(rejected), std::invalid_argument);
    EXPECT_EQ(index.document_count(), 1U);
    EXPECT_EQ(index.unique_term_count(), 1U);
    EXPECT_EQ(index.total_token_count(), 2U);
    EXPECT_TRUE(index.find_postings("new").empty());
    const auto old = index.find_postings("old");
    ASSERT_EQ(old.size(), 1U);
    expect_posting(old.front(), 0, std::array<Position, 2>{0, 1});
    EXPECT_EQ(index.add_document("new old another"), 1U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "new"), 1U);
    EXPECT_EQ(InvertedIndexTestAccess::term_id(index, "another"), 2U);
}

TEST(InvertedIndexTest, ChecksIdLengthCounterAndCapacityBoundariesBeforeArithmetic) {
    using namespace minisearch::detail;
    constexpr std::uintmax_t max_id = std::numeric_limits<std::uint32_t>::max();
    constexpr auto max_count = std::numeric_limits<std::uint64_t>::max();
    constexpr auto max_slots = std::numeric_limits<std::size_t>::max();
    for (const char *error : {"Document ID limit exceeded", "Term ID limit exceeded"}) {
        EXPECT_EQ(checked_index_id(max_id, error), max_id);
        EXPECT_THROW(static_cast<void>(checked_index_id(max_id + 1, error)), std::length_error);
    }
    EXPECT_EQ(checked_document_length(max_id + 1), max_id + 1);
    EXPECT_THROW(static_cast<void>(checked_document_length(max_id + 2)), std::length_error);
    for (const char *error : {"Corpus token count overflow", "Collection frequency overflow"}) {
        EXPECT_EQ(checked_count_add(max_count - 1, 1, error), max_count);
        EXPECT_THROW(static_cast<void>(checked_count_add(max_count, 1, error)), std::length_error);
    }
    EXPECT_EQ(append_capacity(3, 4, max_slots, 1), 4U);
    EXPECT_EQ(append_capacity(4, 4, max_slots, 1), 8U);
    EXPECT_EQ(append_capacity(max_slots - 1, max_slots - 1, max_slots, 1), max_slots);
    EXPECT_THROW(static_cast<void>(append_capacity(max_slots, max_slots, max_slots, 1)),
                 std::length_error);
}

TEST(InvertedIndexTest, HandlesOneMiBWithThousandsOfTermsAndDensePositions) {
    InvertedIndex index;
    constexpr std::uint32_t unique_prefix = 4096;
    constexpr std::string_view repeat = " RepeatedTerm";
    constexpr std::uint32_t repetitions = (1U << 20) / repeat.size() + 1;
    std::string text;
    for (std::uint32_t term = 0; term < unique_prefix; ++term) {
        text += " term" + std::to_string(term);
    }
    for (std::uint32_t position = 0; position < repetitions; ++position) {
        text += repeat;
    }
    ASSERT_GE(text.size(), 1U << 20);
    ASSERT_EQ(index.add_document(text), 0U);
    EXPECT_EQ(index.unique_term_count(), unique_prefix + 1U);
    EXPECT_EQ(index.document_length(0), unique_prefix + repetitions);
    EXPECT_EQ(index.total_token_count(), unique_prefix + repetitions);
    EXPECT_EQ(index.collection_frequency("repeatedterm"), repetitions);
    EXPECT_EQ(index.term_frequency("repeatedterm", 0), repetitions);
    const auto list = index.find_postings("repeatedterm");
    ASSERT_EQ(list.size(), 1U);
    ASSERT_EQ(list.front().positions.size(), repetitions);
    for (std::uint32_t position = 0; position < repetitions; ++position) {
        ASSERT_EQ(list.front().positions[position], unique_prefix + position);
    }
    const auto last_unique = index.find_postings("term4095");
    ASSERT_EQ(last_unique.size(), 1U);
    expect_posting(last_unique.front(), 0, std::array<Position, 1>{unique_prefix - 1});
}

std::vector<std::string> reference_tokens(std::string_view text) {
    constexpr std::string_view upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr std::string_view allowed = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string masked(text);
    for (char &letter : masked) {
        const auto uppercase = upper.find(letter);
        if (uppercase != std::string_view::npos) {
            letter = allowed[uppercase];
        } else if (allowed.find(letter) == std::string_view::npos) {
            letter = ' ';
        }
    }

    std::vector<std::string> tokens;
    auto start = masked.find_first_not_of(' ');
    while (start != std::string::npos) {
        const auto end = std::min(masked.find(' ', start), masked.size());
        tokens.push_back(masked.substr(start, end - start));
        start = masked.find_first_not_of(' ', end);
    }
    return tokens;
}

struct ReferencePosting {
    DocumentId id;
    std::vector<Position> positions;
};

struct ReferenceTerm {
    std::vector<ReferencePosting> postings;
    std::uint64_t count = 0;
};

using Vocabulary = std::map<std::string, ReferenceTerm>;

void expect_corpus(const InvertedIndex &index, const Vocabulary &reference,
                   std::span<const std::uint32_t> lengths) {
    ASSERT_EQ(index.document_count(), lengths.size());
    ASSERT_EQ(index.unique_term_count(), reference.size());
    std::vector<std::vector<bool>> seen;
    seen.reserve(lengths.size());
    for (const auto length : lengths) {
        seen.emplace_back(length);
    }
    std::vector<std::uint64_t> per_document(lengths.size());
    std::uint64_t total_frequency = 0;
    for (const auto &[term, term_reference] : reference) {
        SCOPED_TRACE(term);
        const auto postings = index.find_postings(term);
        ASSERT_EQ(postings.size(), term_reference.postings.size());
        EXPECT_EQ(index.document_frequency(term), postings.size());
        EXPECT_LE(postings.size(), index.document_count());
        std::optional<DocumentId> previous;
        std::uint64_t frequency = 0;
        for (std::size_t offset = 0; offset < postings.size(); ++offset) {
            const auto &posting = postings[offset];
            const auto &expected = term_reference.postings[offset];
            ASSERT_LT(posting.document_id, lengths.size());
            ASSERT_FALSE(posting.positions.empty());
            if (previous) {
                EXPECT_LT(*previous, posting.document_id);
            }
            previous = posting.document_id;

            expect_posting(posting, expected.id, expected.positions);
            EXPECT_EQ(index.term_frequency(term, posting.document_id), posting.positions.size());
            const auto *duplicate_or_unsorted = std::adjacent_find(
                posting.positions.begin(), posting.positions.end(), std::greater_equal<Position>{});
            EXPECT_EQ(duplicate_or_unsorted, posting.positions.end());
            for (const auto position : posting.positions) {
                ASSERT_LT(position, lengths[posting.document_id]);
                ASSERT_FALSE(seen[posting.document_id][position]);
                seen[posting.document_id][position] = true;
            }
            frequency += posting.positions.size();
            per_document[posting.document_id] += posting.positions.size();
        }
        EXPECT_EQ(index.collection_frequency(term), frequency);
        EXPECT_EQ(frequency, term_reference.count);
        EXPECT_LE(frequency, index.total_token_count());
        total_frequency += frequency;
    }

    EXPECT_EQ(total_frequency, index.total_token_count());
    std::uint64_t total_length = 0;
    for (std::size_t id = 0; id < lengths.size(); ++id) {
        EXPECT_EQ(index.document_length(static_cast<DocumentId>(id)), lengths[id]);
        EXPECT_EQ(per_document[id], lengths[id]);
        EXPECT_TRUE(std::ranges::all_of(seen[id], [](bool appears) { return appears; }));
        total_length += lengths[id];
    }
    EXPECT_EQ(total_length, index.total_token_count());
}

TEST(InvertedIndexTest, MatchesIndependentReferenceForOverTenThousandGeneratedDocuments) {
    constexpr std::uint32_t seed = 0x502C9A17U;
    std::mt19937 random(seed);
    struct Workload {
        DocumentId documents;
        std::uint32_t vocabulary;
    };
    for (const auto [documents, vocabulary] :
         {Workload{137, 31}, Workload{1024, 337}, Workload{10'000, 3079}}) {
        SCOPED_TRACE(::testing::Message() << "seed=" << seed << " corpus=" << documents);
        InvertedIndex index;
        Vocabulary reference;
        std::vector<std::uint32_t> lengths;
        std::uint64_t total = 0;

        for (DocumentId id = 0; id < documents; ++id) {
            SCOPED_TRACE(id);
            std::string text;
            if (id % 11 != 0) {
                text = "COMMON";
                const auto words = random() % 17U;
                for (std::uint32_t word = 0; word < words; ++word) {
                    text += word % 2 == 0 ? "-w" : ".W";
                    text += std::to_string(random() % vocabulary);
                }
            }
            const auto tokens = reference_tokens(text);
            ASSERT_EQ(index.add_document(text), id);
            lengths.push_back(static_cast<std::uint32_t>(tokens.size()));
            total += tokens.size();
            EXPECT_EQ(index.document_length(id), tokens.size());
            EXPECT_EQ(index.document_count(), static_cast<std::uint64_t>(id) + 1);
            EXPECT_EQ(index.total_token_count(), total);
            EXPECT_DOUBLE_EQ(index.average_document_length(),
                             static_cast<double>(total) / static_cast<double>(lengths.size()));

            for (std::size_t position = 0; position < tokens.size(); ++position) {
                auto &term = reference[tokens[position]];
                if (term.postings.empty() || term.postings.back().id != id) {
                    term.postings.push_back({id, {}});
                }
                term.postings.back().positions.push_back(static_cast<Position>(position));
                ++term.count;
            }
            EXPECT_EQ(index.unique_term_count(), reference.size());
            for (const auto &term : tokens) {
                SCOPED_TRACE(term);
                const auto &expected = reference.at(term);
                const auto postings = index.find_postings(term);
                ASSERT_EQ(postings.size(), expected.postings.size());
                ASSERT_FALSE(postings.empty());
                expect_posting(postings.back(), id, expected.postings.back().positions);
                EXPECT_EQ(index.document_frequency(term), expected.postings.size());
                EXPECT_EQ(index.collection_frequency(term), expected.count);
                EXPECT_EQ(index.term_frequency(term, id),
                          expected.postings.back().positions.size());
            }
        }
        expect_corpus(index, reference, lengths);
    }
}

} // namespace

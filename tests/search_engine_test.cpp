#include "minisearch/search/search_engine.hpp"

#include "minisearch/index/inverted_index.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using minisearch::BM25Config;
using minisearch::BM25Scorer;
using minisearch::DocumentId;
using minisearch::DynamicArray;
using minisearch::InvertedIndex;
using minisearch::ScoredDocument;
using minisearch::search;

constexpr auto unlimited = std::numeric_limits<std::size_t>::max();

std::vector<ScoredDocument> rank_bm25_results(const DynamicArray<ScoredDocument> &raw) {
    std::vector<ScoredDocument> ranked;
    for (const auto &record : raw) {
        ranked.push_back(record);
    }
    std::ranges::sort(ranked, [](const ScoredDocument &left, const ScoredDocument &right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.document_id < right.document_id;
    });
    return ranked;
}

TEST(SearchEngineTest, MatchesHandCalculatedScoresAndHonorsEveryLimitBoundary) {
    InvertedIndex index;
    index.add_document("Python Python Python");
    index.add_document("Python supports data structures and algorithms");
    index.add_document("Database systems");

    EXPECT_TRUE(search(index, "python", 0).empty());
    const auto one = search(index, "python", 1);
    ASSERT_EQ(one.size(), 1U);
    EXPECT_EQ(one[0].document_id, 0U);
    EXPECT_NEAR(one[0].score, 0.7685194478207298, 1e-15);
    for (const auto k : {std::size_t{2}, std::size_t{3}, unlimited}) {
        const auto result = search(index, "python", k);
        ASSERT_EQ(result.size(), 2U);
        EXPECT_EQ(result[0], one[0]);
        EXPECT_EQ(result[1].document_id, 1U);
        EXPECT_NEAR(result[1].score, 0.3729209123851410, 1e-15);
    }
}

TEST(SearchEngineTest, NormalizesAndDeduplicatesTermsWithExactTiesAndUnknownTerms) {
    InvertedIndex index;
    index.add_document("Python database");
    index.add_document("unrelated");
    index.add_document("Python database");
    index.add_document("database");

    const auto expected = search(index, "Python database", unlimited);
    ASSERT_EQ(expected.size(), 3U);
    EXPECT_EQ(expected[0].document_id, 0U);
    EXPECT_EQ(expected[1].document_id, 2U);
    EXPECT_EQ(expected[2].document_id, 3U);
    EXPECT_EQ(expected[0].score, expected[1].score);
    for (const auto query :
         {"database PYTHON", "python python database", "pytHon--DATABASE? unknown"}) {
        EXPECT_TRUE(std::ranges::equal(search(index, query, unlimited), expected));
        const auto first = search(index, query, 1);
        ASSERT_EQ(first.size(), 1U);
        EXPECT_EQ(first[0], expected[0]);
    }
}

TEST(SearchEngineTest, ValidatesConfigFirstAndAsciiEvenAtZeroLimitAndOnEmptyIndex) {
    InvertedIndex index;
    EXPECT_TRUE(search(index, "word", unlimited).empty());
    EXPECT_TRUE(search(index, "word", 0).empty());
    EXPECT_THROW(static_cast<void>(search(index, "\x80", 0)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(search(index, "\x80", 1)), std::invalid_argument);
    for (const auto config : {BM25Config{.k1 = -1.0}, BM25Config{.b = 1.01},
                              BM25Config{.k1 = std::numeric_limits<double>::infinity()},
                              BM25Config{.b = std::numeric_limits<double>::quiet_NaN()}}) {
        for (const auto k : {0U, 2U}) {
            EXPECT_THROW(static_cast<void>(search(index, "word", k, config)),
                         std::invalid_argument);
        }
    }
    try {
        static_cast<void>(search(index, "\x80", 0, {.b = -1.0}));
        FAIL() << "Invalid config was accepted";
    } catch (const std::invalid_argument &error) {
        EXPECT_STREQ(error.what(), "BM25 requires finite k1 >= 0 and finite b in [0, 1]");
    }

    index.add_document("");
    index.add_document(".,!");
    EXPECT_TRUE(search(index, "word", 2).empty());
    index.add_document("word");
    EXPECT_THROW(static_cast<void>(search(index, "word\x80", 0)), std::invalid_argument);
    for (const auto query : {"", "... - ?", "doesnotmatch"}) {
        EXPECT_TRUE(search(index, query, unlimited).empty());
    }
    ASSERT_EQ(search(index, "word", unlimited).size(), 1U);
}

TEST(SearchEngineTest, UsesCustomParametersAndCurrentCorpusAndOwnsResults) {
    DynamicArray<ScoredDocument> original;
    ScoredDocument saved;
    {
        InvertedIndex index;
        index.add_document("term term extra");
        index.add_document("term");
        original = search(index, std::string("TERM"), 1);
        ASSERT_EQ(original.size(), 1U);
        saved = original[0];

        constexpr BM25Config config{.k1 = 2.0, .b = 1.0};
        const BM25Scorer scorer(config);
        const auto custom = search(index, "term", unlimited, config);
        const auto raw = scorer.score_query(index, "term");
        ASSERT_EQ(custom.size(), 2U);
        ASSERT_EQ(raw.size(), 2U);
        EXPECT_EQ(custom[0], raw[1]);
        EXPECT_EQ(custom[1], raw[0]);
        EXPECT_NE(custom[0].score, search(index, "term", unlimited)[0].score);

        index.add_document("");
        index.add_document("term term term");
        const auto after = search(index, "term", unlimited, config);
        ASSERT_EQ(after.size(), 3U);
        EXPECT_TRUE(
            std::ranges::equal(after, rank_bm25_results(scorer.score_query(index, "term"))));
        EXPECT_NE(after[0].score, custom[0].score);
        EXPECT_EQ(original[0], saved);
    }
    ASSERT_EQ(original.size(), 1U);
    EXPECT_EQ(original[0], saved);
}

TEST(SearchEngineTest, MatchesSortedBm25BitForBitAndKeepsPrefixesAcrossSeededCorpora) {
    constexpr std::uint32_t seed = 0xB2570F7U;
    std::mt19937 random(seed);
    for (std::size_t sample = 0; sample < 1'024; ++sample) {
        SCOPED_TRACE(::testing::Message() << "seed=" << seed << " sample=" << sample);
        InvertedIndex index;
        const auto documents = random() % 28U;
        for (std::size_t id = 0; id < documents; ++id) {
            std::string document;
            const auto words = random() % 16U;
            for (std::size_t token = 0; token < words; ++token) {
                document += " w" + std::to_string(random() % 11U);
            }
            index.add_document(document);
        }
        std::string query;
        const auto words = random() % 15U;
        for (std::size_t token = 0; token < words; ++token) {
            query += "--W" + std::to_string(random() % 18U);
        }

        const auto config = BM25Config{static_cast<double>(random() % 501U) / 100.0,
                                       static_cast<double>(random() % 101U) / 100.0};
        const auto raw = BM25Scorer(config).score_query(index, query);
        for (std::size_t row = 1; row < raw.size(); ++row) {
            ASSERT_LT(raw[row - 1].document_id, raw[row].document_id);
        }
        const auto expected = rank_bm25_results(raw);
        const auto all = search(index, query, unlimited, config);
        ASSERT_EQ(all.size(), expected.size());
        for (std::size_t row = 0; row < all.size(); ++row) {
            using ScoreBytes = std::array<std::byte, sizeof(double)>;
            EXPECT_EQ(all[row].document_id, expected[row].document_id);
            EXPECT_EQ(std::bit_cast<ScoreBytes>(all[row].score),
                      std::bit_cast<ScoreBytes>(expected[row].score));
            EXPECT_TRUE(std::isfinite(all[row].score));
            EXPECT_GE(all[row].score, 0.0);
        }

        const auto mid = static_cast<std::size_t>(random() % (all.size() + 2));
        const std::array limits{std::size_t{0}, std::size_t{1}, mid};
        for (const auto k : limits) {
            SCOPED_TRACE(k);
            const auto result = search(index, query, k, config);
            const auto count = std::min(k, expected.size());
            ASSERT_EQ(result.size(), count);
            EXPECT_TRUE(std::ranges::equal(result, std::span{expected}.first(count)));
            EXPECT_TRUE(std::ranges::equal(result, std::span{all}.first(count)));
        }
    }
}

TEST(SearchEngineTest, CoversSparseAndDenseQueriesOverTenThousandDocuments) {
    constexpr DocumentId documents = 10'000;
    InvertedIndex index;
    for (DocumentId id = 0; id < documents; ++id) {
        auto document = "common word" + std::to_string(id);
        if (id == 8100) {
            document += " rare";
        }
        ASSERT_EQ(index.add_document(document), id);
    }
    EXPECT_EQ(index.unique_term_count(), documents + 2U);

    const auto sparse = search(index, "rare", unlimited);
    ASSERT_EQ(sparse.size(), 1U);
    EXPECT_EQ(sparse[0].document_id, 8100U);
    const auto expected = rank_bm25_results(BM25Scorer{}.score_query(index, "common"));
    ASSERT_EQ(expected.size(), documents);
    EXPECT_EQ(expected[0].document_id, 0U);
    EXPECT_EQ(expected.back().document_id, 8100U);
    for (const auto k :
         {std::size_t{1}, std::size_t{10}, std::size_t{100}, static_cast<std::size_t>(documents)}) {
        SCOPED_TRACE(k);
        const auto dense = search(index, "COMMON common missing", k);
        EXPECT_EQ(dense.size(), k);
        EXPECT_TRUE(std::ranges::equal(dense, std::span{expected}.first(k)));
    }
}

} // namespace

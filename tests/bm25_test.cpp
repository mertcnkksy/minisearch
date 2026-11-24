#include "minisearch/ranking/bm25.hpp"

#include "minisearch/data_structures/min_heap.hpp"
#include "minisearch/index/inverted_index.hpp"
#include "ranking/bm25_math.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using minisearch::BM25Config;
using minisearch::BM25Scorer;
using minisearch::DocumentId;
using minisearch::DynamicArray;
using minisearch::InvertedIndex;
using minisearch::ScoredDocument;

static_assert(std::is_trivially_copyable_v<ScoredDocument>);
static_assert(std::is_default_constructible_v<ScoredDocument>);
static_assert(std::is_nothrow_move_constructible_v<ScoredDocument>);
static_assert(std::is_nothrow_move_assignable_v<ScoredDocument>);
static_assert(std::is_same_v<decltype(ScoredDocument::document_id), DocumentId>);

void expect_score(double actual, long double expected) {
    EXPECT_TRUE(std::isfinite(actual));
    EXPECT_GE(actual, 0.0);
    // Allow rounding from double arithmetic, log1p, and a small number of summed terms.
    const long double tolerance = 2e-15L + 2e-14L * std::abs(expected);
    EXPECT_LE(std::abs(static_cast<long double>(actual) - expected), tolerance)
        << "actual=" << actual << " expected=" << expected;
}

TEST(BM25Test, MatchesTheHandCalculatedCorpusAndMultipleTermContributions) {
    InvertedIndex index;
    ASSERT_EQ(index.add_document("Python Python Python"), 0U);
    ASSERT_EQ(index.add_document("Python supports data structures and algorithms"), 1U);
    ASSERT_EQ(index.add_document("Database systems"), 2U);
    ASSERT_EQ(index.document_count(), 3U);
    ASSERT_EQ(index.total_token_count(), 11U);
    ASSERT_EQ(index.document_frequency("python"), 2U);
    EXPECT_DOUBLE_EQ(index.average_document_length(), 11.0 / 3.0);

    const BM25Scorer scorer;
    const auto python = scorer.score_query(index, "python");
    ASSERT_EQ(python.size(), 2U);
    EXPECT_EQ(python[0].document_id, 0U);
    EXPECT_EQ(python[1].document_id, 1U);
    expect_score(python[0].score, 0.76851944782072975665L);
    expect_score(python[1].score, 0.37292091238514099667L);
    EXPECT_DOUBLE_EQ(scorer.score_document(index, 0, "python"), python[0].score);
    EXPECT_DOUBLE_EQ(scorer.score_document(index, 1, "python"), python[1].score);
    EXPECT_DOUBLE_EQ(scorer.score_document(index, 2, "python"), 0.0);

    const auto combined = scorer.score_query(index, "python supports systems");
    ASSERT_EQ(combined.size(), 3U);
    // Independent formula for the rare term in a length-six or length-two document.
    const auto rare_idf = std::log(8.0L / 3.0L);
    expect_score(combined[0].score, 0.76851944782072975665L);
    expect_score(combined[1].score,
                 0.37292091238514099667L +
                     rare_idf * 2.2L / (1.0L + 1.2L * (0.25L + 0.75L * 18.0L / 11.0L)));
    expect_score(combined[2].score,
                 rare_idf * 2.2L / (1.0L + 1.2L * (0.25L + 0.75L * 6.0L / 11.0L)));
}

TEST(BM25Test, PositiveIdfRewardsRareTermsAndAgreesForEqualDocumentFrequencies) {
    InvertedIndex index;
    index.add_document("rare other common");
    index.add_document("common");
    index.add_document("common stuff");
    const BM25Scorer binary({.k1 = 0.0});
    const auto rare = binary.score_document(index, 0, "rare");
    const auto other = binary.score_document(index, 0, "other");
    const auto common = binary.score_document(index, 0, "common");
    expect_score(rare, std::log(8.0L / 3.0L));
    expect_score(common, std::log(8.0L / 7.0L));
    EXPECT_DOUBLE_EQ(rare, other);
    EXPECT_GT(rare, common);
    EXPECT_GT(common, 0.0);
    EXPECT_TRUE(binary.score_query(index, "absent").empty());
}

TEST(BM25Test, FrequencySaturatesWhenDocumentLengthsAreEqual) {
    InvertedIndex index;
    index.add_document("term filler filler filler");
    index.add_document("term term filler filler");
    index.add_document("term term term term");
    const BM25Scorer scorer;
    const auto scores = scorer.score_query(index, "term");
    ASSERT_EQ(scores.size(), 3U);
    EXPECT_LT(scores[0].score, scores[1].score);
    EXPECT_LT(scores[1].score, scores[2].score);
    EXPECT_GT(scores[1].score - scores[0].score, (scores[2].score - scores[1].score) / 2.0);
    EXPECT_LT(scores[2].score, std::log(8.0 / 7.0) * 2.2);
}

TEST(BM25Test, LengthNormalizationPenalizesLongerDocumentsAndCanBeDisabled) {
    InvertedIndex index;
    index.add_document("term filler filler filler");
    index.add_document("term");
    index.add_document("term");
    const BM25Scorer normalized({.k1 = 2.0, .b = 1.0});
    const auto scores = normalized.score_query(index, "term");
    ASSERT_EQ(scores.size(), 3U);
    EXPECT_EQ(scores[0].document_id, 0U);
    EXPECT_EQ(scores[1].document_id, 1U);
    EXPECT_EQ(scores[2].document_id, 2U);
    EXPECT_LT(scores[0].score, scores[1].score);
    EXPECT_DOUBLE_EQ(scores[1].score, scores[2].score);
    const BM25Scorer binary({.k1 = 0.0, .b = 1.0});
    EXPECT_DOUBLE_EQ(binary.score_document(index, 0, "term"),
                     binary.score_document(index, 1, "term"));
    const BM25Scorer unnormalized({.k1 = 2.0, .b = 0.0});
    EXPECT_DOUBLE_EQ(unnormalized.score_document(index, 0, "term"),
                     unnormalized.score_document(index, 1, "term"));
}

TEST(BM25Test, QueryOrderCasePunctuationAndDuplicatesDoNotChangeBits) {
    InvertedIndex index;
    index.add_document("gamma BETA beta alpha");
    index.add_document("Beta alpha GAmma alpha");
    index.add_document("beta beta");
    index.add_document("unused");
    const BM25Scorer scorer;
    const auto expected = scorer.score_query(index, "alpha beta gamma");
    ASSERT_EQ(expected.size(), 3U);
    for (const auto query :
         {"beta gamma alpha", "GAMMA! beta-alpha beta", "gamma gamma alpha beta nOnExIsTeNt"}) {
        SCOPED_TRACE(query);
        const auto actual = scorer.score_query(index, query);
        ASSERT_TRUE(std::ranges::equal(expected, actual));
        for (const auto &result : expected) {
            EXPECT_EQ(scorer.score_document(index, result.document_id, query),
                      scorer.score_document(index, result.document_id, "alpha beta gamma"));
            EXPECT_DOUBLE_EQ(scorer.score_document(index, result.document_id, query), result.score);
        }
    }
    EXPECT_DOUBLE_EQ(scorer.score_document(index, 3, "alpha beta gamma"), 0.0);
}

TEST(BM25Test, RejectsNonFiniteAndOutOfRangeParameters) {
    constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
    constexpr auto infinity = std::numeric_limits<double>::infinity();
    for (const auto config :
         {BM25Config{-1.0, 0.5}, BM25Config{nan, 0.5}, BM25Config{infinity, 0.5},
          BM25Config{-infinity, 0.5}, BM25Config{1.0, -0.1}, BM25Config{1.0, 1.1},
          BM25Config{1.0, nan}, BM25Config{1.0, infinity}, BM25Config{1.0, -infinity}}) {
        EXPECT_THROW(static_cast<void>(BM25Scorer(config)), std::invalid_argument);
    }
}

TEST(BM25Test, ScoresWithZeroSubnormalMinimumAndMaximumK1) {
    InvertedIndex index;
    index.add_document("x x x y");
    index.add_document("");
    EXPECT_DOUBLE_EQ(index.average_document_length(), 2.0);
    EXPECT_EQ(index.document_frequency("x"), 1U);

    constexpr auto largest = std::numeric_limits<double>::max();
    const auto idf = std::log(2.0L);
    for (const auto k1 : {0.0, std::numeric_limits<double>::denorm_min(),
                          std::numeric_limits<double>::min(), largest}) {
        for (const auto b : {0.0, 1.0}) {
            SCOPED_TRACE(::testing::Message() << "k1=" << k1 << " b=" << b);
            const BM25Scorer scorer({k1, b});
            const auto results = scorer.score_query(index, "x");
            ASSERT_EQ(results.size(), 1U);
            const long double factor = k1 == largest ? (b == 0.0 ? 3.0L : 1.5L) : 1.0L;
            expect_score(results[0].score, idf * factor);
            EXPECT_DOUBLE_EQ(scorer.score_document(index, 0, "x"), results[0].score);
            EXPECT_DOUBLE_EQ(scorer.score_document(index, 1, "x"), 0.0);
            if (k1 == 0.0) {
                EXPECT_DOUBLE_EQ(scorer.score_document(index, 0, "x"),
                                 scorer.score_document(index, 0, "y"));
            }
        }
    }
}

TEST(BM25Test, MathPreservesSmallIdfsAndWideStatistics) {
    constexpr auto max_count = std::numeric_limits<std::uint64_t>::max();
    const double tiny = std::ldexp(1.0, -65);
    EXPECT_NEAR(minisearch::detail::positive_bm25_idf(max_count, max_count), tiny, tiny * 1e-15);
    EXPECT_NEAR(minisearch::detail::positive_bm25_idf(max_count, max_count - 1), 3.0 * tiny,
                tiny * 3e-15);
    EXPECT_DOUBLE_EQ(minisearch::detail::positive_bm25_idf(3, 2), 0.47000362924573555365);
    EXPECT_THROW(static_cast<void>(minisearch::detail::positive_bm25_idf(2, 3)), std::logic_error);
    EXPECT_DOUBLE_EQ(minisearch::detail::bm25_tf_factor(0, 0, 0.0, {}), 0.0);
    constexpr BM25Config config{std::numeric_limits<double>::max(), 1.0};
    const double average = std::ldexp(1.0, 63);
    EXPECT_NEAR(minisearch::detail::bm25_tf_factor(max_count - 1, max_count, average, config),
                average, average * 1e-15);

    constexpr auto last_id = std::numeric_limits<DocumentId>::max();
    const ScoredDocument last{last_id, tiny};
    EXPECT_EQ(last.document_id, last_id);
}

TEST(BM25Test, HandlesEmptyAndInvalidQueriesAndValidatesDocumentIdFirst) {
    InvertedIndex index;
    const BM25Scorer scorer;
    EXPECT_TRUE(scorer.score_query(index, "alpha").empty());
    EXPECT_THROW(static_cast<void>(scorer.score_query(index, "\x80")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(scorer.score_document(index, 0, "\x80")), std::out_of_range);
    EXPECT_THROW(static_cast<void>(scorer.score_document(index, 0, "")), std::out_of_range);
    EXPECT_THROW(
        static_cast<void>(scorer.score_document(index, std::numeric_limits<DocumentId>::max(), "")),
        std::out_of_range);

    index.add_document("");
    index.add_document("...");
    EXPECT_TRUE(scorer.score_query(index, "alpha").empty());
    EXPECT_DOUBLE_EQ(scorer.score_document(index, 0, "alpha"), 0.0);
    EXPECT_THROW(static_cast<void>(scorer.score_document(index, 0, "\x80")), std::invalid_argument);

    index.add_document("alpha");
    EXPECT_TRUE(scorer.score_query(index, "").empty());
    EXPECT_TRUE(scorer.score_query(index, " , - ! ").empty());
    EXPECT_DOUBLE_EQ(scorer.score_document(index, 2, " , - ! "), 0.0);
    EXPECT_THROW(static_cast<void>(scorer.score_query(index, "alpha\x80")), std::invalid_argument);
    ASSERT_EQ(scorer.score_query(index, "alpha").size(), 1U);
}

TEST(BM25Test, ReadsFreshCorpusStatsAndReturnsOwnedResults) {
    const BM25Scorer scorer;
    DynamicArray<ScoredDocument> original;
    {
        InvertedIndex index;
        index.add_document("alpha beta");
        original = scorer.score_query(index, std::string("ALPHA"));
        ASSERT_EQ(original.size(), 1U);
        expect_score(original[0].score, std::log(4.0L / 3.0L));

        index.add_document("");
        const auto with_empty = scorer.score_query(index, "alpha");
        ASSERT_EQ(with_empty.size(), 1U);
        // N becomes two and the average falls from two tokens to one.
        expect_score(with_empty[0].score, std::log(2.0L) * 2.2L / (1.0L + 1.2L * 1.75L));
        EXPECT_NE(with_empty[0].score, original[0].score);

        index.add_document("alpha alpha");
        const auto after = scorer.score_query(index, "alpha");
        ASSERT_EQ(after.size(), 2U);
        EXPECT_EQ(after[1].document_id, 2U);
        EXPECT_DOUBLE_EQ(after[0].score, scorer.score_document(index, 0, "alpha"));
        EXPECT_DOUBLE_EQ(after[1].score, scorer.score_document(index, 2, "alpha"));
        ASSERT_EQ(index.document_frequency("alpha"), 2U);
    }
    EXPECT_EQ(original[0].document_id, 0U);
    expect_score(original[0].score, std::log(4.0L / 3.0L));
}

TEST(BM25Test, ScoresSparseAndDenseQueriesInALargeCorpus) {
    InvertedIndex index;
    constexpr DocumentId document_count = 10'000;
    for (DocumentId id = 0; id < document_count; ++id) {
        if (id == 8100) {
            ASSERT_EQ(index.add_document("common group rare"), id);
        } else if (id == 47) {
            ASSERT_EQ(index.add_document("common group"), id);
        } else {
            ASSERT_EQ(index.add_document("common"), id);
        }
    }
    const BM25Scorer scorer;
    const auto sparse = scorer.score_query(index, "rare");
    ASSERT_EQ(sparse.size(), 1U);
    EXPECT_EQ(sparse.capacity(), 1U);
    EXPECT_EQ(sparse[0].document_id, 8100U);
    const auto group = scorer.score_query(index, "rare group");
    ASSERT_EQ(group.size(), 2U);
    EXPECT_EQ(group[0].document_id, 47U);
    EXPECT_EQ(group[1].document_id, 8100U);
    EXPECT_GT(group[1].score, sparse[0].score);

    std::string query;
    for (int repeat = 0; repeat < 10'000; ++repeat) {
        query += "CoMMon -- ";
    }
    const auto dense = scorer.score_query(index, query);
    ASSERT_EQ(dense.size(), document_count);
    for (DocumentId id = 0; id < document_count; ++id) {
        ASSERT_EQ(dense[id].document_id, id);
        ASSERT_TRUE(std::isfinite(dense[id].score));
        ASSERT_GT(dense[id].score, 0.0);
    }
    EXPECT_DOUBLE_EQ(dense[8100].score, scorer.score_document(index, 8100, "common"));
}

using Words = std::vector<std::string>;

std::string render(const Words &words) {
    std::string text;
    for (const auto &word : words) {
        text += " -?! ";
        text += 'W';
        text.append(word, 1);
    }
    return text;
}

std::vector<long double> reference_scores(const std::vector<Words> &corpus, const Words &raw_query,
                                          BM25Config config) {
    std::vector<long double> results(corpus.size());
    std::size_t total = 0;
    for (const auto &document : corpus) {
        total += document.size();
    }
    if (total == 0) {
        return results;
    }

    const auto count = static_cast<long double>(corpus.size());
    const auto average = static_cast<long double>(total) / count;
    const std::set<std::string> query(raw_query.begin(), raw_query.end());
    for (const auto &term : query) {
        std::vector<long double> frequencies;
        long double df = 0.0L;
        for (const auto &document : corpus) {
            const auto frequency = std::ranges::count(document, term);
            frequencies.push_back(static_cast<long double>(frequency));
            if (frequency != 0) {
                df += 1.0L;
            }
        }
        const auto idf = std::log((count + 1.0L) / (df + 0.5L));
        const long double k1 = config.k1;
        const long double b = config.b;
        for (std::size_t id = 0; id < corpus.size(); ++id) {
            const auto tf = frequencies[id];
            if (tf > 0.0L) {
                const auto length = static_cast<long double>(corpus[id].size());
                results[id] +=
                    idf * tf * (k1 + 1.0L) / (tf + k1 * ((1.0L - b) + b * length / average));
            }
        }
    }
    return results;
}

TEST(BM25Test, MatchesIndependentLongDoubleReferenceAcross1024CorporaAndQueries) {
    constexpr std::uint32_t seed = 0xB625D1FFU;
    std::mt19937 random(seed);
    for (int sample = 0; sample < 1024; ++sample) {
        SCOPED_TRACE(::testing::Message() << "seed=" << seed << " sample=" << sample);
        InvertedIndex index;
        std::vector<Words> corpus(random() % 17U);
        const auto vocabulary = 1U + random() % 21U;
        for (auto &document : corpus) {
            document.resize(random() % 18U);
            for (auto &word : document) {
                word = "w" + std::to_string(random() % vocabulary);
            }
            index.add_document(render(document));
        }
        Words query(random() % 11U);
        for (auto &word : query) {
            word = "w" + std::to_string(random() % (vocabulary + 4U));
        }
        const auto config = BM25Config{static_cast<double>(random() % 401U) / 100.0,
                                       static_cast<double>(random() % 101U) / 100.0};
        const BM25Scorer scorer(config);
        const auto text = render(query);
        const auto actual = scorer.score_query(index, text);
        const auto expected = reference_scores(corpus, query, config);
        std::size_t row = 0;
        for (std::size_t id = 0; id < expected.size(); ++id) {
            SCOPED_TRACE(id);
            const double single = scorer.score_document(index, static_cast<DocumentId>(id), text);
            if (expected[id] == 0.0L) {
                EXPECT_DOUBLE_EQ(single, 0.0);
                continue;
            }
            ASSERT_LT(row, actual.size());
            ASSERT_EQ(actual[row].document_id, id);
            expect_score(actual[row].score, expected[id]);
            EXPECT_DOUBLE_EQ(single, actual[row].score);
            ++row;
        }
        ASSERT_EQ(actual.size(), row);
    }
}

struct WorstScoreFirst {
    bool operator()(const ScoredDocument &left, const ScoredDocument &right) const noexcept {
        return left.score < right.score ||
               (left.score == right.score && left.document_id > right.document_id);
    }
};

TEST(BM25Test, ScoreRecordsSupportTheFutureWorstFirstHeapContract) {
    const std::array scores{ScoredDocument{9, 1.0}, ScoredDocument{5, 0.5}, ScoredDocument{7, 1.0}};
    const WorstScoreFirst compare;
    EXPECT_FALSE(compare(scores[0], scores[0]));
    EXPECT_TRUE(compare(scores[1], scores[0]));
    EXPECT_TRUE(compare(scores[0], scores[2]));
    EXPECT_TRUE(compare(scores[1], scores[2]));

    minisearch::MinHeap<ScoredDocument, WorstScoreFirst> heap;
    for (const auto &record : scores) {
        heap.push(record);
    }
    EXPECT_EQ(heap.top().document_id, 5U);
    heap.pop();
    EXPECT_EQ(heap.top().document_id, 9U);
    heap.pop();
    EXPECT_EQ(heap.top().document_id, 7U);
}

} // namespace

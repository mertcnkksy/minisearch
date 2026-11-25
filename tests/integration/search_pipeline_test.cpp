#include "fixtures/golden_corpus.hpp"
#include "minisearch/index/inverted_index.hpp"
#include "minisearch/search/search_engine.hpp"
#include "reference_search.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>

namespace minisearch::test {
namespace {

constexpr auto unlimited = std::numeric_limits<std::size_t>::max();

void expect_statistics(const InvertedIndex &index, const ReferenceSearch &reference) {
    EXPECT_EQ(index.document_count(), reference.documents());
    EXPECT_EQ(index.unique_term_count(), reference.terms());
    EXPECT_EQ(index.total_token_count(), reference.tokens());
    EXPECT_DOUBLE_EQ(index.average_document_length(),
                     static_cast<double>(reference.average_length()));
}

void expect_term(const InvertedIndex &index, const ReferenceSearch &reference,
                 const std::string &term) {
    const auto frequencies = reference.frequencies(term);
    EXPECT_EQ(index.document_frequency(term), frequencies.documents);
    EXPECT_EQ(index.collection_frequency(term), frequencies.occurrences);
    const auto postings = index.find_postings(term);
    ASSERT_EQ(postings.size(), frequencies.documents);
    std::size_t row = 0;
    for (std::size_t id = 0; id < reference.documents(); ++id) {
        const auto positions = reference.positions(id, term);
        EXPECT_EQ(index.term_frequency(term, static_cast<DocumentId>(id)), positions.size());
        if (!positions.empty()) {
            ASSERT_LT(row, postings.size());
            EXPECT_EQ(postings[row].document_id, id);
            EXPECT_TRUE(std::ranges::equal(postings[row].positions, positions));
            ++row;
        }
    }
    EXPECT_EQ(row, postings.size());
}

void expect_query(const InvertedIndex &index, const ReferenceSearch &reference,
                  std::string_view query, std::span<const std::size_t> limits,
                  BM25Config config = {}) {
    SCOPED_TRACE(::testing::Message()
                 << "query=" << query << " k1=" << config.k1 << " b=" << config.b);
    const auto expected = reference.rank(query, config.k1, config.b);
    auto full_scores = BM25Scorer(config).score_query(index, query);
    auto by_id = expected;
    std::ranges::sort(by_id, {}, &ReferenceScore::id);
    ASSERT_EQ(full_scores.size(), by_id.size());
    for (std::size_t row = 0; row < by_id.size(); ++row) {
        ASSERT_EQ(full_scores[row].document_id, by_id[row].id);
        const auto actual = full_scores[row].score;
        EXPECT_TRUE(std::isfinite(actual));
        EXPECT_GE(actual, 0);
        EXPECT_LE(std::abs(actual - by_id[row].score), score_tolerance(by_id[row].score));
    }
    if (!full_scores.empty()) {
        std::ranges::sort(full_scores, [](const ScoredDocument &left, const ScoredDocument &right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.document_id < right.document_id;
        });
    }
    for (std::size_t row = 0; row < expected.size(); ++row) {
        if (full_scores[row].document_id != expected[row].id) {
            const auto candidate = std::ranges::lower_bound(by_id, full_scores[row].document_id, {},
                                                            &ReferenceScore::id);
            ASSERT_NE(candidate, by_id.end());
            EXPECT_LE(std::abs(candidate->score - expected[row].score),
                      score_tolerance(candidate->score) + score_tolerance(expected[row].score));
        }
    }
    for (const auto k : limits) {
        SCOPED_TRACE(k);
        const auto actual = search(index, query, k, config);
        // Exact production-double order independently checks membership at close cutoffs.
        ASSERT_TRUE(
            std::ranges::equal(actual, std::span(full_scores).first(std::min(k, by_id.size()))));
    }
}

TEST(SearchPipelineIntegration, GoldenManifestMatchesIndependentPostingsAndBothRankingChecks) {
    InvertedIndex index;
    ReferenceSearch reference;
    constexpr std::array limits{std::size_t{0}, std::size_t{1}, std::size_t{5}, unlimited};
    expect_query(index, reference, "database", limits);
    for (const auto text : golden_documents) {
        EXPECT_EQ(index.add_document(text), reference.add(text));
    }
    expect_statistics(index, reference);
    for (std::size_t id = 0; id < reference.documents(); ++id) {
        EXPECT_EQ(index.document_length(static_cast<DocumentId>(id)), reference.length(id));
    }
    for (const auto *term : {"python", "index", "quokka", "unobtainium", "16"}) {
        expect_term(index, reference, term);
    }
    for (const auto query : golden_queries) {
        for (const auto config :
             {BM25Config{}, BM25Config{.k1 = 0.0, .b = 1.0}, BM25Config{.k1 = 1.8, .b = 0.0},
              BM25Config{.k1 = std::numeric_limits<double>::max(), .b = 1.0}}) {
            expect_query(index, reference, query, limits, config);
        }
    }
    EXPECT_THROW(static_cast<void>(search(index, "python", 1, {.b = 2.0})), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(reference.rank("python", 1.2, 2.0)), std::invalid_argument);
}

TEST(SearchPipelineIntegration, ExactTieAndNumericallyCloseCutoffRespectRankOrder) {
    InvertedIndex index;
    ReferenceSearch reference;
    for (const auto *text : {"word", "word word", "word word"}) {
        EXPECT_EQ(index.add_document(text), reference.add(text));
    }
    constexpr std::array limits{std::size_t{1}, std::size_t{2}, unlimited};
    constexpr BM25Config config{.k1 = 8e-16, .b = 0};
    expect_query(index, reference, "word", limits, config);
    const auto all = search(index, "word", unlimited, config);
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[0].document_id, 1U);
    EXPECT_EQ(all[1].document_id, 2U);
    EXPECT_EQ(all[2].document_id, 0U);
    EXPECT_EQ(all[0].score, all[1].score);
    EXPECT_GT(all[1].score, all[2].score);
    EXPECT_EQ(search(index, "word", 1, config)[0], all[0]);
}

TEST(SearchPipelineIntegration, MetamorphicQueriesOwnershipAndCorpusEvolution) {
    ReferenceSearch reference;
    DynamicArray<ScoredDocument> saved;
    const auto populate = [&reference](InvertedIndex &index) {
        for (const auto text : golden_documents) {
            index.add_document(text);
        }
        EXPECT_EQ(index.document_count(), reference.documents());
    };
    for (const auto text : golden_documents) {
        reference.add(text);
    }
    {
        InvertedIndex index;
        populate(index);
        saved = search(index, "python tree", unlimited);
        for (const auto query : {"TREE python", "python python tree", "python--tree",
                                 "python tree neverindexed", "python tree"}) {
            EXPECT_TRUE(std::ranges::equal(search(index, query, unlimited), saved));
            EXPECT_TRUE(std::ranges::equal(search(index, query, 2), std::span(saved).first(2)));
        }
        EXPECT_THROW(index.add_document("invalid \xFF"), std::invalid_argument);
        EXPECT_TRUE(std::ranges::equal(search(index, "python tree", unlimited), saved));
        EXPECT_EQ(index.add_document("unmatched quasar"), reference.add("unmatched quasar"));
        constexpr std::array limits{unlimited};
        expect_query(index, reference, "python tree", limits);
        const auto after = search(index, "python tree", unlimited);
        ASSERT_FALSE(after.empty());
        EXPECT_NE(after[0].score, saved[0].score);
    }
    InvertedIndex rebuilt;
    for (const auto text : golden_documents) {
        rebuilt.add_document(text);
    }
    EXPECT_TRUE(std::ranges::equal(search(rebuilt, "python tree", unlimited), saved));
}

TEST(SearchPipelineIntegration, StatefulOperationsUseReproducibleBoundedCorpora) {
    constexpr std::uint32_t seed = 0xA990010U;
    std::mt19937 random(seed);
    constexpr std::size_t sessions = 64;
    constexpr std::size_t steps = 160;
    for (std::size_t session = 0; session < sessions; ++session) {
        InvertedIndex index;
        ReferenceSearch reference;
        std::string last;
        for (std::size_t operation = 0; operation < steps; ++operation) {
            const auto choice = random() % 9U;
            const auto word = "key" + std::to_string(random() % 45U);
            const auto input = choice == 0   ? "common " + word + " ID" + std::to_string(operation)
                               : choice == 1 ? last
                               : choice == 2 ? std::string{}
                                             : "common " + word + " common absent";
            SCOPED_TRACE(::testing::Message()
                         << "seed=" << seed << " session=" << session << " operation=" << operation
                         << " input=" << input << " choice=" << choice);
            if (choice < 3) {
                EXPECT_EQ(index.add_document(input), reference.add(input));
                last = input;
            } else if (choice == 3 || choice == 4) {
                const auto before = search(index, "common", 3);
                if (choice == 3) {
                    EXPECT_THROW(index.add_document(input + '\x80'), std::invalid_argument);
                    EXPECT_THROW(reference.add(input + '\x80'), std::invalid_argument);
                } else {
                    EXPECT_THROW(static_cast<void>(search(index, input + '\x80', 0)),
                                 std::invalid_argument);
                }
                EXPECT_TRUE(std::ranges::equal(before, search(index, "common", 3)));
            } else if (choice < 8) {
                const std::array limits{std::size_t{0}, std::size_t{1}, std::size_t{10}, unlimited};
                const auto limit = random() % limits.size();
                const double k1 = static_cast<double>(random() % 7U) / 2;
                const double b = static_cast<double>(random() % 5U) / 4;
                expect_query(index, reference, choice == 5 ? word : input,
                             std::span(limits).subspan(limit, 1), {.k1 = k1, .b = b});
            } else {
                expect_term(index, reference, word);
            }
            expect_statistics(index, reference);
        }
    }
}

TEST(SearchPipelineIntegration, LargeCorpusCrossChecksSparseAndDenseQueriesAfterGrowth) {
    ReferenceSearch reference;
    InvertedIndex index;
    for (std::size_t id = 0; id < 10'000; ++id) {
        auto document = "common item" + std::to_string(id);
        if (id % 7 == 0) {
            document += " common common";
        }
        if (id % 1007 == 0) {
            document += " scarce";
        }
        ASSERT_EQ(index.add_document(document), reference.add(document));
    }
    expect_statistics(index, reference);
    expect_term(index, reference, "scarce");
    constexpr std::array limits{std::size_t{1}, std::size_t{10}, std::size_t{100}, unlimited};
    for (const auto query : {"item5555", "scarce", "common", "common scarce item5555"}) {
        expect_query(index, reference, query, limits);
    }
}

} // namespace
} // namespace minisearch::test

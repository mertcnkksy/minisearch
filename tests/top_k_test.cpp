#include "minisearch/data_structures/min_heap.hpp"
#include "search/top_k.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace {

using minisearch::DocumentId;
using minisearch::MinHeap;
using minisearch::ScoredDocument;
using minisearch::detail::select_top_k;
using minisearch::detail::WorstScoreFirst;

constexpr auto unlimited = std::numeric_limits<std::size_t>::max();
constexpr auto last_id = std::numeric_limits<DocumentId>::max();
constexpr WorstScoreFirst worse;
static_assert(noexcept(worse(ScoredDocument{}, ScoredDocument{})));

std::vector<ScoredDocument> reference_top_k(std::vector<ScoredDocument> candidates, std::size_t k) {
    std::ranges::sort(candidates, [](const auto &left, const auto &right) {
        if (left.score == right.score) {
            return left.document_id < right.document_id;
        }
        return left.score > right.score;
    });
    candidates.resize(std::min(k, candidates.size()));
    return candidates;
}

TEST(TopKTest, HandlesEmptySingleAndOversizedLimits) {
    constexpr std::array single{ScoredDocument{last_id, 2.42}};
    EXPECT_TRUE(select_top_k({}, 0).empty());
    EXPECT_TRUE(select_top_k({}, unlimited).empty());
    EXPECT_TRUE(select_top_k(single, 0).empty());
    for (const auto k : {std::size_t{1}, std::size_t{2}, unlimited}) {
        const auto result = select_top_k(single, k);
        ASSERT_EQ(result.size(), 1U);
        EXPECT_EQ(result.capacity(), 1U);
        EXPECT_EQ(result[0], single[0]);
    }
}

TEST(TopKTest, SelectsTheExpectedExampleInEveryPermutation) {
    std::array candidates{
        ScoredDocument{0, 2.42}, ScoredDocument{1, 5.71}, ScoredDocument{2, 1.36},
        ScoredDocument{3, 4.82}, ScoredDocument{4, 0.91}, ScoredDocument{5, 3.17},
        ScoredDocument{6, 5.71}, ScoredDocument{7, 2.16},
    };
    constexpr std::array best{ScoredDocument{1, 5.71}, ScoredDocument{6, 5.71},
                              ScoredDocument{3, 4.82}};
    do {
        const auto three = select_top_k(candidates, 3);
        ASSERT_TRUE(std::ranges::equal(three, best));
        const auto one = select_top_k(candidates, 1);
        ASSERT_EQ(one.size(), 1U);
        ASSERT_EQ(one[0], best[0]);
    } while (std::ranges::next_permutation(candidates, {}, &ScoredDocument::document_id).found);
}

TEST(TopKTest, WorstFirstComparatorIsStrictOnFiniteScoresAndTreatsSignedZeroAsEqual) {
    const auto adjacent = std::nextafter(1.0, 2.0);
    const std::array samples{
        ScoredDocument{0, 0.0},
        ScoredDocument{0, -0.0},
        ScoredDocument{0, 0.0},
        ScoredDocument{last_id, -0.0},
        ScoredDocument{last_id, std::numeric_limits<double>::denorm_min()},
        ScoredDocument{0, 1.0},
        ScoredDocument{last_id, 1.0},
        ScoredDocument{last_id, adjacent},
        ScoredDocument{0, std::numeric_limits<double>::max()},
    };
    const auto equivalent = [](const auto &left, const auto &right) {
        return !worse(left, right) && !worse(right, left);
    };
    for (const auto &a : samples) {
        EXPECT_FALSE(worse(a, a));
        for (const auto &b : samples) {
            EXPECT_FALSE(worse(a, b) && worse(b, a));
            for (const auto &c : samples) {
                if (worse(a, b) && worse(b, c)) {
                    EXPECT_TRUE(worse(a, c));
                }
                if (equivalent(a, b) && equivalent(b, c)) {
                    EXPECT_TRUE(equivalent(a, c));
                }
            }
        }
    }
    EXPECT_TRUE(equivalent(samples[0], samples[1]));
    EXPECT_TRUE(worse(samples[3], samples[0]));
    EXPECT_TRUE(worse(samples[3], samples[4]));
    EXPECT_TRUE(worse(samples[5], samples[7]));
    EXPECT_TRUE(worse(samples[6], samples[5]));
    EXPECT_TRUE(worse(samples[7], samples[8]));

    const std::array candidates{ScoredDocument{0, 1.0}, ScoredDocument{1, -0.0},
                                ScoredDocument{last_id, adjacent}, ScoredDocument{2, 0.0}};
    const auto ranked = select_top_k(candidates, unlimited);
    ASSERT_EQ(ranked.size(), candidates.size());
    EXPECT_EQ(ranked[0], candidates[2]);
    EXPECT_EQ(ranked[1], candidates[0]);
    EXPECT_EQ(ranked[2], candidates[1]);
    EXPECT_TRUE(std::signbit(ranked[2].score));
    EXPECT_EQ(ranked[3], candidates[3]);
    EXPECT_FALSE(std::signbit(ranked[3].score));
}

TEST(TopKTest, BoundedHeapKeepsTheWorstSelectedResultAtTheRootAfterEachCandidate) {
    constexpr std::array candidates{
        ScoredDocument{1, 1.0},  ScoredDocument{4, 2.0},  ScoredDocument{5, 2.0},
        ScoredDocument{2, 3.0},  ScoredDocument{6, 4.0},  ScoredDocument{7, 5.0},
        ScoredDocument{8, 0.0},  ScoredDocument{9, 1.0},  ScoredDocument{last_id, 5.0},
        ScoredDocument{11, 5.0}, ScoredDocument{12, 5.0}, ScoredDocument{0, 5.0},
    };
    constexpr std::size_t k = 3;
    MinHeap<ScoredDocument, WorstScoreFirst> heap;
    heap.reserve(k);
    std::vector<ScoredDocument> seen;
    for (const auto &candidate : candidates) {
        seen.push_back(candidate);
        const auto expected = reference_top_k(seen, k);
        if (heap.size() < k) {
            heap.push(candidate);
        } else if (worse(heap.top(), candidate)) {
            heap.pop();
            heap.push(candidate);
        }
        ASSERT_EQ(heap.size(), expected.size());
        EXPECT_EQ(heap.capacity(), k);
        EXPECT_EQ(heap.top(), expected.back());
        EXPECT_TRUE(std::ranges::equal(select_top_k(seen, k), expected));
    }
    const std::array best{ScoredDocument{0, 5.0}, ScoredDocument{7, 5.0}, ScoredDocument{11, 5.0}};
    EXPECT_TRUE(std::ranges::equal(select_top_k(candidates, k), best));
}

TEST(TopKTest, UsesOnlyDocumentIdsWhenAllCandidatesHaveEqualScores) {
    constexpr DocumentId count = 1'024;
    std::vector<ScoredDocument> candidates;
    candidates.reserve(count);
    for (DocumentId id = count; id != 0; --id) {
        candidates.push_back({last_id - (count - id), 2.0});
    }
    for (const auto k : {std::size_t{1}, std::size_t{10}, unlimited}) {
        const auto result = select_top_k(candidates, k);
        ASSERT_EQ(result.size(), std::min(k, candidates.size()));
        EXPECT_TRUE(std::ranges::equal(result, reference_top_k(candidates, k)));
        EXPECT_EQ(result[0].document_id, last_id - count + 1U);
    }
}

TEST(TopKTest, MatchesAnIndependentSortAcrossTenThousandSeededCandidateAndLimitPairs) {
    constexpr std::uint32_t seed = 0x70B4C75U;
    std::mt19937 random(seed);
    for (std::size_t sample = 0; sample < 10'000; ++sample) {
        const auto count = static_cast<std::size_t>(random() % 81U);
        std::vector<ScoredDocument> candidates;
        candidates.reserve(count);
        for (std::size_t row = 0; row < count; ++row) {
            const auto id = last_id - static_cast<DocumentId>(row);
            const auto score = static_cast<double>(row % 7);
            candidates.push_back({id, score});
        }
        std::ranges::shuffle(candidates, random);
        if (sample % 3 != 0) {
            for (std::size_t row = 0; row < count; ++row) {
                const auto score = sample % 3 == 1 ? row : count - row;
                candidates[row].score = static_cast<double>(score);
            }
        }

        const std::array limits{std::size_t{0}, std::size_t{1},
                                count,          count + 7,
                                unlimited,      static_cast<std::size_t>(random() % 96U)};
        const auto k = limits[(sample / 3) % limits.size()];
        SCOPED_TRACE(::testing::Message() << "seed=" << seed << " sample=" << sample
                                          << " count=" << count << " k=" << k);
        const auto actual = select_top_k(candidates, k);
        const auto expected = reference_top_k(candidates, k);
        ASSERT_EQ(actual.size(), expected.size());
        ASSERT_TRUE(std::ranges::equal(actual, expected));
    }
}

TEST(TopKTest, SelectsFromOneHundredThousandCandidatesWithDenseTies) {
    constexpr DocumentId count = 100'000;
    std::vector<ScoredDocument> candidates;
    candidates.reserve(count);
    for (DocumentId id = count; id != 0; --id) {
        candidates.push_back({id, static_cast<double>(id % 32)});
    }
    for (const auto k :
         {std::size_t{1}, std::size_t{10}, std::size_t{100}, static_cast<std::size_t>(count)}) {
        SCOPED_TRACE(k);
        const auto result = select_top_k(candidates, k);
        EXPECT_EQ(result.capacity(), k);
        EXPECT_TRUE(std::ranges::equal(result, reference_top_k(candidates, k)));
    }
}

} // namespace

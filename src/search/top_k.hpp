#pragma once

#include "minisearch/data_structures/dynamic_array.hpp"
#include "minisearch/ranking/scored_document.hpp"

#include <cstddef>
#include <span>

namespace minisearch::detail {

// Requires finite, nonnegative scores. The worse record belongs closer to the heap root.
struct WorstScoreFirst {
    [[nodiscard]] bool operator()(const ScoredDocument &left,
                                  const ScoredDocument &right) const noexcept {
        return left.score < right.score ||
               (left.score == right.score && left.document_id > right.document_id);
    }
};

// Requires unique document IDs and finite, nonnegative scores. Returns the best
// min(k, candidates.size()) records in rank order without changing the input.
[[nodiscard]] DynamicArray<ScoredDocument> select_top_k(std::span<const ScoredDocument> candidates,
                                                        std::size_t k);

} // namespace minisearch::detail

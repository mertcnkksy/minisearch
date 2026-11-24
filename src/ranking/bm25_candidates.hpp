#pragma once

#include "minisearch/ranking/bm25.hpp"

namespace minisearch::detail {

void validate_bm25_config(const BM25Config &config);

// Requires validated parameters. Returns one owned, finite, nonnegative score
// per matching document, with no ordering guarantee.
[[nodiscard]] DynamicArray<ScoredDocument> collect_bm25_candidates(const InvertedIndex &index,
                                                                   std::string_view query,
                                                                   const BM25Config &config);

} // namespace minisearch::detail

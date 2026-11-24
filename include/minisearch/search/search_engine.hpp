#pragma once

#include "minisearch/ranking/bm25.hpp"

#include <cstddef>
#include <string_view>

namespace minisearch {

// Returns up to k owned BM25 matches, best score first and smaller document ID first on ties.
// Validates the config before the ASCII query, including when k is zero.
// Allocation and validation errors propagate. Keep the index unchanged during the call.
[[nodiscard]] DynamicArray<ScoredDocument>
search(const InvertedIndex &index, std::string_view query, std::size_t k, BM25Config config = {});

} // namespace minisearch

#pragma once

#include "minisearch/data_structures/dynamic_array.hpp"
#include "minisearch/ranking/scored_document.hpp"

#include <string_view>

namespace minisearch {

class InvertedIndex;

struct BM25Config {
    double k1 = 1.2;
    double b = 0.75;
};

// Scores ASCII queries using the index's current corpus statistics.
class BM25Scorer {
  public:
    // Throws std::invalid_argument unless k1 is finite and nonnegative and b is in [0, 1].
    explicit BM25Scorer(BM25Config config = {});

    // Checks the document ID before tokenizing. A nonmatching document scores zero.
    [[nodiscard]] double score_document(const InvertedIndex &index, DocumentId document_id,
                                        std::string_view query) const;

    // Returns owned results in ascending document ID order.
    // Keep the index unchanged for the duration of either scoring call.
    [[nodiscard]] DynamicArray<ScoredDocument> score_query(const InvertedIndex &index,
                                                           std::string_view query) const;

  private:
    BM25Config config_;
};

} // namespace minisearch

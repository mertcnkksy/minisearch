#pragma once

#include "minisearch/index/document.hpp"
#include "minisearch/ranking/bm25.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace minisearch::detail {

[[nodiscard]] inline double positive_bm25_idf(std::uint64_t document_count,
                                              std::uint64_t document_frequency) {
    if (document_frequency > document_count) {
        throw std::logic_error("BM25 document frequency exceeds corpus size");
    }
    const auto missing = document_count - document_frequency;
    return std::log1p((static_cast<double>(missing) + 0.5) /
                      (static_cast<double>(document_frequency) + 0.5));
}

// Requires a validated config and, for a match that needs normalization, valid corpus lengths.
[[nodiscard]] inline double bm25_tf_factor(std::uint64_t frequency, DocumentLength length,
                                           double average_length, const BM25Config &config) {
    if (frequency == 0) {
        return 0.0;
    }
    if (config.k1 == 0.0) {
        return 1.0;
    }
    if (!(average_length > 0.0)) {
        throw std::logic_error("BM25 matching document requires a positive average length");
    }

    const double tf = static_cast<double>(frequency);
    const double normalization =
        1.0 - config.b + config.b * (static_cast<double>(length) / average_length);
    if (config.k1 > 1.0) {
        // Divide both sides of the fraction by k1 before multiplying by TF.
        return tf * (1.0 + 1.0 / config.k1) / (tf / config.k1 + normalization);
    }
    return tf * (1.0 + config.k1) / (tf + config.k1 * normalization);
}

} // namespace minisearch::detail

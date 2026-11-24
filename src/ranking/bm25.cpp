#include "minisearch/ranking/bm25.hpp"

#include "bm25_math.hpp"
#include "minisearch/data_structures/hash_table.hpp"
#include "minisearch/index/inverted_index.hpp"
#include "minisearch/text/tokenizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace minisearch {

namespace {

std::vector<Token> distinct_query_terms(std::string_view query) {
    auto terms = Tokenizer::tokenize(query);
    std::ranges::sort(terms, {}, &Token::term);
    const auto duplicates = std::ranges::unique(terms, {}, &Token::term);
    terms.erase(duplicates.begin(), duplicates.end());
    return terms;
}

} // namespace

BM25Scorer::BM25Scorer(BM25Config config) : config_(config) {
    if (!std::isfinite(config.k1) || config.k1 < 0.0 || !std::isfinite(config.b) ||
        config.b < 0.0 || config.b > 1.0) {
        throw std::invalid_argument("BM25 requires finite k1 >= 0 and finite b in [0, 1]");
    }
}

double BM25Scorer::score_document(const InvertedIndex &index, DocumentId document_id,
                                  std::string_view query) const {
    const auto length = index.document_length(document_id);
    const auto terms = distinct_query_terms(query);
    const auto document_count = index.document_count();
    const auto average_length = index.average_document_length();
    if (length == 0) {
        return 0.0;
    }

    double score = 0.0;
    for (const auto &token : terms) {
        const auto postings = index.find_postings(token.term);
        if (postings.empty()) {
            continue;
        }
        // Reuse the same sorted list for DF and the target document's TF.
        const auto match =
            std::ranges::lower_bound(postings, document_id, {}, &Posting::document_id);
        if (match != postings.end() && match->document_id == document_id) {
            const auto idf = detail::positive_bm25_idf(document_count, postings.size());
            score += idf * detail::bm25_tf_factor(match->positions.size(), length, average_length,
                                                  config_);
        }
    }
    return score;
}

DynamicArray<ScoredDocument> BM25Scorer::score_query(const InvertedIndex &index,
                                                     std::string_view query) const {
    const auto terms = distinct_query_terms(query);
    const auto document_count = index.document_count();
    const auto average_length = index.average_document_length();
    if (average_length == 0.0) {
        return {};
    }

    using ResultIndex = std::size_t;
    HashTable<DocumentId, ResultIndex> rows;
    DynamicArray<ScoredDocument> scores;
    for (const auto &token : terms) {
        const auto postings = index.find_postings(token.term);
        if (postings.empty()) {
            continue;
        }
        const auto idf = detail::positive_bm25_idf(document_count, postings.size());
        for (const auto &posting : postings) {
            const auto length = index.document_length(posting.document_id);
            const double contribution =
                idf *
                detail::bm25_tf_factor(posting.positions.size(), length, average_length, config_);
            if (const auto *row = rows.find(posting.document_id); row != nullptr) {
                scores[*row].score += contribution;
            } else {
                rows.insert(posting.document_id, scores.size());
                scores.push_back({posting.document_id, contribution});
            }
        }
    }

    // Row indices are needed only while accumulating.
    if (scores.size() > 1) {
        std::ranges::sort(scores, {}, &ScoredDocument::document_id);
    }
    return scores;
}

} // namespace minisearch

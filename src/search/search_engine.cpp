#include "minisearch/search/search_engine.hpp"

#include "../ranking/bm25_candidates.hpp"
#include "minisearch/text/tokenizer.hpp"
#include "top_k.hpp"

#include <span>

namespace minisearch {

DynamicArray<ScoredDocument> search(const InvertedIndex &index, std::string_view query,
                                    std::size_t k, BM25Config config) {
    detail::validate_bm25_config(config);
    if (k == 0) {
        static_cast<void>(Tokenizer::tokenize(query));
        return {};
    }

    const auto candidates = detail::collect_bm25_candidates(index, query, config);
    return detail::select_top_k(std::span{candidates}, k);
}

} // namespace minisearch

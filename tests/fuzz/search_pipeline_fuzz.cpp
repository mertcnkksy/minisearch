#include "check.hpp"
#include "minisearch/index/inverted_index.hpp"
#include "minisearch/search/search_engine.hpp"
#include "reference_search.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    using namespace minisearch;
    using test::must;
    if (size < 3 || size > 1024) {
        return 0;
    }
    const auto document_count = bytes[0] % 9U;
    const std::size_t k =
        bytes[1] % 5U == 4U ? std::numeric_limits<std::size_t>::max() : bytes[1] % 5U;
    const auto k1 =
        bytes[2] % 9U == 8U ? std::numeric_limits<double>::max() : (bytes[2] % 9U) / 4.0;
    const auto b = (bytes[2] % 5U) / 4.0;
    InvertedIndex index;
    test::ReferenceSearch reference;
    std::string_view input(reinterpret_cast<const char *>(bytes + 3), size - 3);
    for (unsigned i = 0; i < document_count && !input.empty(); ++i) {
        const auto length = static_cast<unsigned char>(input.front()) % 97U;
        input.remove_prefix(1);
        const auto document = input.substr(0, length);
        input.remove_prefix(document.size());
        const bool valid = test::is_ascii(document);
        try {
            const auto id = index.add_document(document);
            must(valid && id == reference.add(document), "Wrong document acceptance or ID");
        } catch (const std::invalid_argument &) {
            must(!valid, "Valid document was rejected");
        }
        must(index.document_count() == reference.documents() &&
                 index.total_token_count() == reference.tokens() &&
                 index.unique_term_count() == reference.terms(),
             "Corpus state changed unexpectedly");
    }
    const auto query = input.substr(0, 192);
    const bool valid = test::is_ascii(query);
    try {
        const auto actual = search(index, query, k, {.k1 = k1, .b = b});
        must(valid, "Invalid query was accepted");
        auto expected = reference.rank(query, k1, b);
        auto scores = BM25Scorer({.k1 = k1, .b = b}).score_query(index, query);
        must(scores.size() == expected.size(), "Wrong full match set size");
        std::ranges::sort(expected, {}, &test::ReferenceScore::id);
        for (std::size_t i = 0; i < expected.size(); ++i) {
            must(scores[i].document_id == expected[i].id && std::isfinite(scores[i].score) &&
                     scores[i].score >= 0 &&
                     std::abs(scores[i].score - expected[i].score) <=
                         test::score_tolerance(expected[i].score),
                 "Wrong match ID or arithmetic");
        }
        if (!scores.empty()) {
            std::ranges::sort(scores, [](const auto &a, const auto &b_score) {
                return a.score != b_score.score ? a.score > b_score.score
                                                : a.document_id < b_score.document_id;
            });
        }
        must(actual.size() == std::min(k, scores.size()), "Wrong cutoff");
        for (std::size_t i = 0; i < actual.size(); ++i) {
            must(actual[i] == scores[i], "Wrong exact ranking or duplicate ID");
        }
    } catch (const std::invalid_argument &) {
        must(!valid, "Valid query was rejected");
    }
    return 0;
}

#include "reference_search.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace minisearch::test {

bool is_ascii(std::string_view text) {
    return std::ranges::all_of(text, [](unsigned char byte) { return byte <= 127; });
}

std::vector<ReferenceToken> reference_tokens(std::string_view text) {
    if (!is_ascii(text)) {
        throw std::invalid_argument("Expected ASCII");
    }
    constexpr std::string_view alphabet =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr std::string_view folded =
        "0123456789abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
    std::vector<ReferenceToken> tokens;
    for (auto begin = text.find_first_of(alphabet); begin != text.npos;) {
        const auto stop = text.find_first_not_of(alphabet, begin);
        const auto end = stop == text.npos ? text.size() : stop;
        std::string word;
        word.reserve(end - begin);
        for (const char byte : text.substr(begin, end - begin)) {
            word += folded[alphabet.find(byte)];
        }
        tokens.push_back({std::move(word), begin, end});
        begin = text.find_first_of(alphabet, end);
    }
    return tokens;
}

std::size_t ReferenceSearch::add(std::string_view document) {
    auto words = reference_tokens(document);
    std::set<std::string_view> seen;
    for (const auto &word : words) {
        auto &frequency = frequencies_[word.term];
        ++frequency.occurrences;
        if (seen.insert(word.term).second) {
            ++frequency.documents;
        }
    }
    token_count_ += words.size();
    documents_.push_back(std::move(words));
    return documents_.size() - 1;
}

long double ReferenceSearch::average_length() const noexcept {
    return documents_.empty() ? 0.0L : static_cast<long double>(token_count_) / documents_.size();
}

Frequencies ReferenceSearch::frequencies(std::string_view term) const {
    const auto found = frequencies_.find(term);
    return found == frequencies_.end() ? Frequencies{} : found->second;
}

std::vector<std::size_t> ReferenceSearch::positions(std::size_t id, std::string_view term) const {
    std::vector<std::size_t> result;
    const auto &document = documents_.at(id);
    for (std::size_t position = 0; position < document.size(); ++position) {
        if (document[position].term == term) {
            result.push_back(position);
        }
    }
    return result;
}

std::vector<ReferenceScore> ReferenceSearch::rank(std::string_view query, double k1,
                                                  double b) const {
    if (!std::isfinite(k1) || !std::isfinite(b) || k1 < 0 || b < 0 || b > 1) {
        throw std::invalid_argument("Unexpected BM25 parameters");
    }
    const auto words = reference_tokens(query);
    std::map<std::string_view, long double> idfs;
    const auto n = static_cast<long double>(documents());
    for (const auto &word : words) {
        const auto df = frequencies(word.term).documents;
        if (df != 0) {
            idfs.try_emplace(word.term, std::log((n + 1) / (df + 0.5L)));
        }
    }

    std::vector<ReferenceScore> scores;
    if (tokens() == 0 || idfs.empty()) {
        return scores;
    }
    const long double k = k1;
    const auto half = 1 / (1 + k);
    const auto length_weight = k * half;
    const auto average = average_length();
    for (std::size_t id = 0; id < documents(); ++id) {
        const auto &document = documents_[id];
        const long double length = document.size();
        const auto normalized = (1.0L - b) + b * length / average;
        long double sum = 0;
        for (const auto &[term, idf] : idfs) {
            const auto tf = std::ranges::count(document, term, &ReferenceToken::term);
            if (tf != 0) {
                // A weighted reciprocal avoids forming tf * k or k * normalized.
                sum += idf / (half + length_weight * normalized / tf);
            }
        }
        if (sum != 0) {
            scores.push_back({id, sum});
        }
    }
    std::ranges::sort(scores, [](const ReferenceScore &left, const ReferenceScore &right) {
        if (left.score == right.score) {
            return left.id < right.id;
        }
        return left.score > right.score;
    });
    return scores;
}

long double score_tolerance(long double score) {
    // The fixtures use few query terms and well-conditioned positive contributions.
    return 8 * std::numeric_limits<double>::epsilon() +
           24 * std::numeric_limits<double>::epsilon() * std::abs(score);
}

} // namespace minisearch::test

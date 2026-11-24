#pragma once

#include "minisearch/data_structures/hash_table.hpp"
#include "minisearch/index/document.hpp"
#include "minisearch/index/posting.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace minisearch {

namespace detail {
struct InvertedIndexTestAccess;
}

// Owns an in-memory, append-only index of ASCII documents.
class InvertedIndex {
  public:
    InvertedIndex() = default;
    InvertedIndex(const InvertedIndex &) = delete;
    InvertedIndex &operator=(const InvertedIndex &) = delete;
    InvertedIndex(InvertedIndex &&) = delete;
    InvertedIndex &operator=(InvertedIndex &&) = delete;

    // A failed insertion preserves logical contents and does not consume an ID.
    DocumentId add_document(std::string_view text);

    // Lookups use exact normalized terms. Missing terms return an empty view or zero.
    // Reacquire views and references after any insertion attempt, including a failed one.
    [[nodiscard]] std::span<const Posting> find_postings(const std::string &normalized_term) const;
    [[nodiscard]] std::uint64_t document_frequency(const std::string &normalized_term) const;
    [[nodiscard]] std::uint64_t collection_frequency(const std::string &normalized_term) const;

    // An unknown document ID throws std::out_of_range, even when the term is missing.
    [[nodiscard]] std::uint64_t term_frequency(const std::string &normalized_term,
                                               DocumentId document_id) const;
    [[nodiscard]] DocumentLength document_length(DocumentId document_id) const;

    [[nodiscard]] std::uint64_t document_count() const noexcept;
    [[nodiscard]] std::uint64_t unique_term_count() const noexcept;
    [[nodiscard]] std::uint64_t total_token_count() const noexcept;
    [[nodiscard]] double average_document_length() const noexcept;

  private:
    friend struct detail::InvertedIndexTestAccess;

    using TermId = std::uint32_t;

    struct TermEqual {
        bool operator()(const std::string &left, const std::string &right) const noexcept {
            return left == right;
        }
    };

    struct TermPostings {
        std::vector<Posting> postings;
        std::uint64_t collection_frequency = 0;
    };

    [[nodiscard]] const TermPostings *find_term(const std::string &term) const;

    HashTable<std::string, TermId, std::hash<std::string>, TermEqual> dictionary_;
    std::vector<TermPostings> term_lists_;
    std::vector<DocumentLength> document_lengths_;
    std::uint64_t total_token_count_ = 0;
};

} // namespace minisearch

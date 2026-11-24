#include "minisearch/index/inverted_index.hpp"

#include "index_limits.hpp"
#include "minisearch/text/tokenizer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace minisearch {

namespace {

struct PendingTerm {
    std::size_t first_token;
    Posting posting;
    std::uint32_t id = 0;
    std::uint64_t collection_frequency = 0;
    bool is_new = false;
};

std::vector<PendingTerm> group_terms(const std::vector<Token> &tokens, DocumentId document_id) {
    // Views borrow strings from the immutable token vector only for the duration of grouping.
    HashTable<std::string_view, std::size_t> groups;
    std::vector<PendingTerm> pending;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto &token = tokens[index];
        const auto *group = groups.find(token.term);
        if (group != nullptr) {
            pending[*group].posting.positions.push_back(token.position);
        } else {
            groups.insert(token.term, pending.size());
            pending.push_back({index, {document_id, {}}});
            pending.back().posting.positions.push_back(token.position);
        }
    }
    return pending;
}

template <typename T> void reserve_for_append(std::vector<T> &items, std::size_t additional) {
    const auto target =
        detail::append_capacity(items.size(), items.capacity(), items.max_size(), additional);
    if (target > items.capacity()) {
        items.reserve(target);
    }
}

} // namespace

DocumentId InvertedIndex::add_document(std::string_view text) {
    // Validation and staging leave the index untouched.
    const auto document_id =
        detail::checked_index_id(document_count(), "Document ID limit exceeded");
    const auto tokens = Tokenizer::tokenize(text);
    const auto length = detail::checked_document_length(tokens.size());
    const auto total =
        detail::checked_count_add(total_token_count_, length, "Corpus token count overflow");

    auto pending = group_terms(tokens, document_id);
    auto next_id = unique_term_count();
    std::size_t new_count = 0;
    for (auto &term : pending) {
        const auto frequency = static_cast<std::uint64_t>(term.posting.positions.size());
        const auto *existing = dictionary_.find(tokens[term.first_token].term);
        if (existing != nullptr) {
            term.id = *existing;
            term.collection_frequency =
                detail::checked_count_add(term_lists_[term.id].collection_frequency, frequency,
                                          "Collection frequency overflow");
        } else {
            term.id = detail::checked_index_id(next_id, "Term ID limit exceeded");
            term.collection_frequency = frequency;
            term.is_new = true;
            ++next_id;
            ++new_count;
        }
    }

    // Preparation can change capacity, while existing contents and statistics stay intact.
    reserve_for_append(document_lengths_, 1);
    reserve_for_append(term_lists_, new_count);
    if (new_count != 0) {
        dictionary_.reserve(term_lists_.size() + new_count);
    }
    std::vector<TermPostings> new_lists;
    new_lists.reserve(new_count);
    for (auto &term : pending) {
        if (term.is_new) {
            TermPostings list;
            list.postings.push_back(std::move(term.posting));
            list.collection_frequency = term.collection_frequency;
            new_lists.push_back(std::move(list));
        } else {
            reserve_for_append(term_lists_[term.id].postings, 1);
        }
    }

    // Dictionary insertion can still allocate for keys or for a tombstone cleanup rehash.
    static_assert(std::is_nothrow_invocable_v<const std::hash<std::string> &, const std::string &>);
    static_assert(
        std::is_nothrow_invocable_v<const TermEqual &, const std::string &, const std::string &>);
    std::size_t processed = 0;
    try {
        for (; processed < pending.size(); ++processed) {
            const auto &term = pending[processed];
            if (term.is_new && !dictionary_.insert(tokens[term.first_token].term, term.id)) {
                throw std::logic_error("Duplicate staged term");
            }
        }
    } catch (...) {
        // The prepared prefix is the rollback log. Erase only probes and destroys a key.
        while (processed != 0) {
            const auto &term = pending[--processed];
            if (term.is_new) {
                [[maybe_unused]] const bool removed =
                    dictionary_.erase(tokens[term.first_token].term);
                assert(removed);
            }
        }
        throw;
    }

    // Commit only appends into reserved capacity and moves owned, noexcept records.
    static_assert(std::is_nothrow_move_constructible_v<Posting>);
    static_assert(std::is_nothrow_move_constructible_v<TermPostings>);
    for (auto &term : pending) {
        if (!term.is_new) {
            auto &list = term_lists_[term.id];
            list.postings.push_back(std::move(term.posting));
            list.collection_frequency = term.collection_frequency;
        }
    }
    for (auto &list : new_lists) {
        term_lists_.push_back(std::move(list));
    }
    document_lengths_.push_back(length);
    total_token_count_ = total;
    return document_id;
}

const InvertedIndex::TermPostings *InvertedIndex::find_term(const std::string &term) const {
    const auto *id = dictionary_.find(term);
    return id == nullptr ? nullptr : &term_lists_[*id];
}

std::span<const Posting> InvertedIndex::find_postings(const std::string &normalized_term) const {
    const auto *list = find_term(normalized_term);
    return list == nullptr ? std::span<const Posting>{} : std::span<const Posting>{list->postings};
}

std::uint64_t InvertedIndex::document_frequency(const std::string &normalized_term) const {
    return find_postings(normalized_term).size();
}

std::uint64_t InvertedIndex::collection_frequency(const std::string &normalized_term) const {
    const auto *list = find_term(normalized_term);
    return list == nullptr ? 0 : list->collection_frequency;
}

std::uint64_t InvertedIndex::term_frequency(const std::string &normalized_term,
                                            DocumentId document_id) const {
    static_cast<void>(document_length(document_id));
    const auto postings = find_postings(normalized_term);
    if (postings.empty()) {
        return 0;
    }
    const auto match = std::lower_bound(
        postings.begin(), postings.end(), document_id,
        [](const Posting &posting, DocumentId id) { return posting.document_id < id; });
    return match != postings.end() && match->document_id == document_id ? match->positions.size()
                                                                        : 0;
}

DocumentLength InvertedIndex::document_length(DocumentId document_id) const {
    if (document_id >= document_lengths_.size()) {
        throw std::out_of_range("Unknown document ID");
    }
    return document_lengths_[document_id];
}

std::uint64_t InvertedIndex::document_count() const noexcept { return document_lengths_.size(); }
std::uint64_t InvertedIndex::unique_term_count() const noexcept { return term_lists_.size(); }
std::uint64_t InvertedIndex::total_token_count() const noexcept { return total_token_count_; }

double InvertedIndex::average_document_length() const noexcept {
    return document_lengths_.empty() ? 0.0
                                     : static_cast<double>(total_token_count_) /
                                           static_cast<double>(document_lengths_.size());
}

} // namespace minisearch

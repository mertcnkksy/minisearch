#pragma once

#include "minisearch/index/document.hpp"
#include "minisearch/index/posting.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace minisearch::detail {

[[nodiscard]] constexpr std::uint32_t checked_index_id(std::uintmax_t next, const char *error) {
    if (next > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(error);
    }
    return static_cast<std::uint32_t>(next);
}

[[nodiscard]] constexpr DocumentLength checked_document_length(std::uintmax_t count) {
    constexpr auto limit = static_cast<DocumentLength>(std::numeric_limits<Position>::max()) + 1;
    if (count > limit) {
        throw std::length_error("Document token count exceeds the position range");
    }
    return static_cast<DocumentLength>(count);
}

[[nodiscard]] constexpr std::uint64_t checked_count_add(std::uint64_t count, std::uint64_t added,
                                                        const char *error) {
    if (added > std::numeric_limits<std::uint64_t>::max() - count) {
        throw std::length_error(error);
    }
    return count + added;
}

// All arguments describe a valid vector before the append.
[[nodiscard]] constexpr std::size_t append_capacity(std::size_t size, std::size_t capacity,
                                                    std::size_t limit, std::size_t additional) {
    if (additional > limit - size) {
        throw std::length_error("InvertedIndex vector capacity limit exceeded");
    }
    const auto required = size + additional;
    if (required <= capacity) {
        return capacity;
    }
    const auto doubled = capacity > limit / 2 ? limit : capacity * 2;
    return std::max(required, doubled);
}

} // namespace minisearch::detail

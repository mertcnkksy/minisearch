#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace minisearch {

// An owned, normalized term and its location in the original input.
struct Token {
    std::string term;
    std::uint32_t position = 0;
    std::size_t start_offset = 0;
    std::size_t end_offset = 0;

    bool operator==(const Token &) const = default;
};

} // namespace minisearch

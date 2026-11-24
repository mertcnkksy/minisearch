#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace minisearch::detail {

[[nodiscard]] constexpr std::uint32_t checked_token_position(std::uintmax_t index) {
    if (index > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Tokenizer position exceeds the uint32_t range");
    }
    return static_cast<std::uint32_t>(index);
}

} // namespace minisearch::detail

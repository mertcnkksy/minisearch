#pragma once

#include "minisearch/text/token.hpp"

#include <string_view>
#include <vector>

namespace minisearch {

class Tokenizer {
  public:
    // Returns owned ASCII terms, zero-based positions, and half-open byte offsets.
    // Throws invalid_argument with the offset of the first non-ASCII byte encountered.
    // Capacity and allocation exceptions propagate. A failed call returns no partial result.
    [[nodiscard]] static std::vector<Token> tokenize(std::string_view text);
};

} // namespace minisearch

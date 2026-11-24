#pragma once

#include "minisearch/data_structures/dynamic_array.hpp"
#include "minisearch/index/document.hpp"

#include <cstdint>

namespace minisearch {

using Position = std::uint32_t;

struct Posting {
    DocumentId document_id;
    // Strictly increasing token positions. The length is the term frequency.
    DynamicArray<Position> positions;
};

} // namespace minisearch

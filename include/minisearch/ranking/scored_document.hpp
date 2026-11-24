#pragma once

#include "minisearch/index/document.hpp"

namespace minisearch {

struct ScoredDocument {
    DocumentId document_id = 0;
    double score = 0.0;

    bool operator==(const ScoredDocument &) const = default;
};

} // namespace minisearch

#pragma once

#include <cstdint>

namespace minisearch {

// IDs start at zero. Every value, including UINT32_MAX, is valid.
using DocumentId = std::uint32_t;
using DocumentLength = std::uint64_t;

} // namespace minisearch

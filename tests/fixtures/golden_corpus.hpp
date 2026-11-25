#pragma once

#include <array>
#include <string_view>

namespace minisearch::test {

using namespace std::literals::string_view_literals;

// The position in this manifest is the insertion ID.
inline constexpr std::array golden_documents{
    "Python Python scripts inspect a red-black tree."sv,
    "C++17 programs allocate nodes for a red-black tree."sv,
    "A database stores rows in a B tree index."sv,
    "SQL reads the database with a bitmap index."sv,
    "Python Python scripts inspect a red-black tree."sv,
    ""sv,
    "A scheduler assigns time slices to 4 processes."sv,
    "The kernel maps virtual pages to physical frames."sv,
    "A packet travels between devices on a local network."sv,
    "TCP acknowledges a packet and retransmits lost bytes."sv,
    "A trie indexes shared word prefixes in a dictionary."sv,
    "A hash table resolves collisions with open addressing."sv,
    "A binary heap ranks candidates by document score."sv,
    "An inverted index stores token positions per document."sv,
    "BM25 scores a document using term frequency and length."sv,
    "A query retrieves matches from a positional index."sv,
    "L1 and L2 caches shrink average memory access latency."sv,
    "The processor predicts branches before decoding instructions."sv,
    "A storage engine writes pages to a persistent journal."sv,
    "SQL transactions isolate readers from different writers."sv,
    "RUST emphasizes ownership while Python favors readability."sv,
    "SIMD instructions compare 16 bytes at once!"sv,
    "A cache can be local to a network proxy."sv,
    "A rare quokka token separates this document."sv,
};

inline constexpr std::array golden_queries{
    ""sv,   "python"sv,       "tree index"sv,     "SQL DATABASE"sv, "packet TCP"sv,
    "16"sv, "quokka index"sv, "CACHE--network"sv, "rust"sv,         "unobtainium"sv,
};

} // namespace minisearch::test

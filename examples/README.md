# C++ API examples

Build the static core library from the repository root.

```sh
cmake --preset release-cli
cmake --build --preset release-cli
```

Save each C++ block to its own file using the name in the heading. Compile from the
repository root with the compiler used for the library. For example, after saving
`tokenizer_example.cpp`, run the following commands.

```sh
c++ -std=c++20 -I include tokenizer_example.cpp build/release-cli/libminisearch_core.a -o build/release-cli/tokenizer_example
./build/release-cli/tokenizer_example
```

Use the same command with each example's filename. The three data structures are
header-only. Linking the archive also works with those examples. For a sanitizer
build, use the matching compiler and sanitizer options when compiling and linking
the example, for instance `-fsanitize=address,undefined`, and use the corresponding
build's archive.

Use the headers and archive from the checkout. There is no install or
`find_package` interface in this release.

## DynamicArray — `dynamic_array_example.cpp`

Checks the second element of a contiguous array. See [ADR 0002](../docs/adr/0002-dynamic-array.md) for types and invalidation.

```cpp
#include "minisearch/data_structures/dynamic_array.hpp"

#include <cstdint>

int main() {
    minisearch::DynamicArray<std::uint32_t> ids;
    ids.reserve(2);
    ids.push_back(3);
    ids.push_back(11);

    return ids.at(1) == 11 ? 0 : 1;
}
```

## HashTable — `hash_table_example.cpp`

Checks the value stored under a string key. See [ADR 0003](../docs/adr/0003-open-addressing-hash-table.md) for load limits and value pointers.

```cpp
#include "minisearch/data_structures/hash_table.hpp"

#include <cstdint>
#include <string>

int main() {
    minisearch::HashTable<std::string, std::uint32_t> term_ids;
    term_ids.insert("minisearch", 42U);

    const auto* id = term_ids.find("minisearch");
    return id != nullptr && *id == 42U ? 0 : 1;
}
```

## MinHeap — `min_heap_example.cpp`

Builds an integer heap from a span and checks the next minimum after a pop. See [ADR 0004](../docs/adr/0004-binary-min-heap.md) for ordering and heapify.

```cpp
#include "minisearch/data_structures/min_heap.hpp"

#include <array>
#include <span>

int main() {
    constexpr std::array values{7, 1, 5};
    minisearch::MinHeap<int> heap{std::span<const int>{values}};
    heap.push(3);
    heap.pop();

    return heap.top() == 3 ? 0 : 1;
}
```

## Tokenizer — `tokenizer_example.cpp`

Prints each token's owned lowercase term, zero-based position, and half-open byte range.
Unsupported bytes raise an exception with the first offending byte offset. See [ADR 0005](../docs/adr/0005-ascii-tokenizer.md).

```cpp
#include "minisearch/text/tokenizer.hpp"

#include <iostream>

int main() {
    for (const auto &token : minisearch::Tokenizer::tokenize("Hello, World42!")) {
        std::cout << token.term << " position=" << token.position << " bytes=["
                  << token.start_offset << ", " << token.end_offset << ")\n";
    }
}
```

## InvertedIndex — `inverted_index_example.cpp`

Posting lookups accept exact normalized terms. Reacquire posting views after any
insertion attempt, including a failed one. See [ADR 0006](../docs/adr/0006-positional-inverted-index.md).

```cpp
#include "minisearch/index/inverted_index.hpp"

#include <iostream>

int main() {
    minisearch::InvertedIndex index;
    index.add_document("Python supports data structures.");
    const auto document = index.add_document("Python is fast. Python supports algorithms.");

    std::cout << "DF=" << index.document_frequency("python")
              << " TF=" << index.term_frequency("python", document) << '\n';
    for (const auto &posting : index.find_postings("python")) {
        std::cout << "document " << posting.document_id << " positions";
        for (const auto position : posting.positions) {
            std::cout << ' ' << position;
        }
        std::cout << '\n';
    }
}
```

## BM25Scorer — `bm25_example.cpp`

Raw scores are returned in ascending document ID order. See [ADR 0007](../docs/adr/0007-bm25-ranking.md).

```cpp
#include "minisearch/index/inverted_index.hpp"
#include "minisearch/ranking/bm25.hpp"

#include <iomanip>
#include <iostream>

int main() {
    minisearch::InvertedIndex index;
    index.add_document("Python Python Python");
    index.add_document("Python supports data structures and algorithms");
    index.add_document("Database systems");

    const minisearch::BM25Scorer scorer;
    for (const auto &result : scorer.score_query(index, "python")) {
        std::cout << result.document_id << ' ' << std::fixed << std::setprecision(12)
                  << result.score << '\n';
    }
}
```

## Ranked search

The [ranked search example](../README.md#c-library-usage) calls the public
`minisearch::search` function. It returns owned results in descending score
order, breaking exact ties by ascending document ID.

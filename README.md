# MiniSearch

A search engine project built from scratch in C++20 with custom data structures and information retrieval algorithms.

The project includes the C++20 build and test infrastructure, an interactive CLI, a contiguous
`minisearch::DynamicArray<T>`, an open-addressed `minisearch::HashTable<Key, Value>`, and
an array-backed `minisearch::MinHeap<T>`. It also includes a shared ASCII tokenizer for
documents and queries, an in-memory positional inverted index, Okapi BM25 scoring, and
heap-based Top-K retrieval.
Search features are planned in the [roadmap](ROADMAP.md).

## Build and test

Install a C++20 compiler, CMake 3.24 or newer, and Ninja. The test configuration retrieves
GoogleTest 1.15.2 from GitHub and verifies the archive's SHA-256 checksum.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/minisearch --help
./build/debug/minisearch --version
```

Use the `release` preset for an optimized build and `sanitizers` for a Debug build with
AddressSanitizer and UndefinedBehaviorSanitizer. Set `-DBUILD_TESTING=OFF` at configuration time
to build the core and CLI without downloading GoogleTest.

## Search from the terminal

```sh
cmake --preset release
cmake --build --preset release
./build/release/minisearch
```

Use `help` to see the commands and `exit` or `quit` to leave. The index lives in memory
for one process. Restarting MiniSearch starts an empty index.

Run this demo from the repository root after building. It creates a temporary plain-text
file and runs all commands in one session.

```sh
(
    set -eu
    cli="$PWD/build/release/minisearch"
    demo_dir="$(mktemp -d)"
    trap 'rm -rf -- "$demo_dir"' EXIT
    cd "$demo_dir"
    mkdir documents
    printf 'Database systems\n' > 'documents/database notes.txt'
    "$cli" <<'COMMANDS'
add Python Python Python
add Python supports data structures and algorithms
index documents/database notes.txt
search --top 2 python
stats
exit
COMMANDS
)
```

```text
MiniSearch
In-memory document search engine.
Type 'help' for available commands.

minisearch> Indexed document 0.
minisearch> Indexed document 1.
minisearch> Indexed document 2.
minisearch> Rank  Doc ID  Score
1     0       0.768519
2     1       0.372921

Returned 2 results.
minisearch> Documents:      3
Unique terms:   8
Total tokens:   11
Avg. length:    3.666667
minisearch>
```

Search defaults to 10 results. Use `search -- <query>` when the query starts with an
option-like word. Commands and document content have separate limits. A command line
may contain up to 1 MiB before LF and a file may contain up to 16 MiB. These input
limits allow additional memory for tokenization, indexing, and search.

Use ASCII for queries and document contents. File paths can contain Unicode and internal
spaces. `index` uses paths literally, trimming leading and trailing ASCII spaces or tabs.
Use trusted local files. See [ADR 0009](docs/adr/0009-interactive-cli.md).

## Use DynamicArray

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

See the [DynamicArray decision record](docs/adr/0002-dynamic-array.md) for type requirements,
exception behavior, iterator invalidation, and operation costs. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the development checks.

## Use HashTable

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

See the [HashTable decision record](docs/adr/0003-open-addressing-hash-table.md) for
load limits, pointer invalidation, and complexity.

## Use MinHeap

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

See the [MinHeap decision record](docs/adr/0004-binary-min-heap.md) for ordering,
type requirements, exception behavior, and bottom-up construction costs.

## Tokenize ASCII text

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

Link the example with `minisearch_core`. Each token owns its normalized term and stores
its zero-based position and original half-open byte range. Unsupported bytes raise an
exception with the first offending byte offset. The v0.2.0 Query Engine milestone plans
UTF-8 validation, Unicode tokenization, ICU integration, NFC normalization, and Turkish
language support. See [ADR 0005](docs/adr/0005-ascii-tokenizer.md).

## Index documents and inspect postings

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

Link the example with `minisearch_core`. Lookups accept exact normalized terms. Reacquire
posting views after calling `add_document`. See [ADR 0006](docs/adr/0006-positional-inverted-index.md)
for the API and transaction contract.

## Score an ASCII query with BM25

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

Link the example with `minisearch_core`. The scores are displayed in document ID order.

```text
0 0.768519447821
1 0.372920912385
```

See [ADR 0007](docs/adr/0007-bm25-ranking.md) for the scoring model and parameter contract.

## Retrieve the best matches

Save this example as `search_example.cpp` and link it with `minisearch_core`.

```cpp
#include "minisearch/index/inverted_index.hpp"
#include "minisearch/search/search_engine.hpp"

#include <iomanip>
#include <iostream>

int main() {
    minisearch::InvertedIndex index;
    index.add_document("Database systems");
    index.add_document("Python supports data structures and algorithms");
    index.add_document("Python Python Python");

    for (const auto &result : minisearch::search(index, "PYTHON", 2)) {
        std::cout << result.document_id << ' ' << std::fixed << std::setprecision(12)
                  << result.score << '\n';
    }
}
```

After building the Debug preset, compile and run the example from the repository root.

```sh
c++ -std=c++20 -I include search_example.cpp build/debug/libminisearch_core.a -o build/debug/search_example
./build/debug/search_example
```

```text
2 0.768519447821
1 0.372920912385
```

The query returns up to two matches, with higher scores first and smaller document IDs
first on exact ties. See [ADR 0008](docs/adr/0008-top-k-retrieval.md) for details.

## License

See [LICENSE](LICENSE).

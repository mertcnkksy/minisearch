# MiniSearch

A search engine project built from scratch in C++20 with custom data structures and information retrieval algorithms.

The project includes the C++20 build and test infrastructure, a minimal CLI, a contiguous
`minisearch::DynamicArray<T>`, an open-addressed `minisearch::HashTable<Key, Value>`, and
an array-backed `minisearch::MinHeap<T>`.
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

## License

See [LICENSE](LICENSE).

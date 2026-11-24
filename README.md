# MiniSearch

A search engine project built from scratch in C++20 with custom data structures and information retrieval algorithms.

MS-001 establishes the build and test infrastructure, a minimal command-line executable, and
a contiguous `minisearch::DynamicArray<T>` for types such as `std::uint32_t`. Search features are
planned in the [roadmap](ROADMAP.md).

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

## License

See [LICENSE](LICENSE).

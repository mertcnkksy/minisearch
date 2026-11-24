# Contributing

Use a C++20 compiler, CMake 3.24 or newer, and Ninja. Install clang-format and clang-tidy
for local formatting and analysis. CI uses GCC 14, Clang 18, and LLVM 18 analysis tools on
Ubuntu 24.04.

## Development checks

Run the presets from the repository root.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Repeat with `release` and, on toolchains with sanitizer runtimes, `sanitizers`. The ASan and
UBSan options can also be set individually at configure time. For example, run
`cmake --preset debug -B build/asan-only -DMINISEARCH_ENABLE_ASAN=ON`, then build with
`cmake --build build/asan-only` and test with
`ctest --test-dir build/asan-only --output-on-failure --no-tests=error`. Use a separate
build directory for each compiler or option set to keep CMake caches independent.

Run formatting and opt-in clang-tidy analysis from the root.

```sh
CLANG_FORMAT=clang-format-18 scripts/check-format.sh
CXX=clang++-18 cmake --preset debug -B build/tidy -DMINISEARCH_ENABLE_CLANG_TIDY=ON
cmake --build build/tidy
```

Use `CLANG_FORMAT` to select your installed formatter. If multiple clang-tidy versions are
installed, select one with `-DMINISEARCH_CLANG_TIDY=/path/to/clang-tidy`.

## Changes and verification

Add focused tests for new behavior and document public contracts, error behavior, iterator
invalidation, and complexity when they change. Document architectural decisions in
[docs/adr/](docs/adr/), and record user-visible changes under Unreleased in the
[changelog](CHANGELOG.md). Report the compiler, configuration, and results of checks you
actually ran. See [AGENTS.md](AGENTS.md) for the project rules.

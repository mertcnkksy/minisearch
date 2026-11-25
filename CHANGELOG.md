# Changelog

## Unreleased

## [0.1.0]

### Added

- C++20 CMake targets for the static core library and interactive CLI, with a tests-disabled
  Release preset. The CLI reports `minisearch 0.1.0`.
- A contiguous DynamicArray with checked access, geometric growth, and copy and move support.
- An open-addressed HashTable with linear probing, tombstone reuse, and occupancy-based rebuilding.
- A DynamicArray-backed binary MinHeap with stateful comparators and linear-time bottom-up construction.
- A stateless ASCII tokenizer with owned lowercase terms, zero-based positions, and half-open
  byte offsets. Unsupported bytes are rejected with their offset.
- An append-only positional inverted index with stable document and term IDs, corpus statistics,
  and logical rollback after failed insertion.
- Okapi BM25 with positive IDF, deterministic query term handling, and sparse score accumulation.
- Bounded heap-based Top-K retrieval that breaks exact score ties by ascending document ID.
- An interactive session with text and file indexing, ranked search, corpus statistics, help,
  bounded input, and recoverable command errors.

### Testing

- Debug, Release, and ASan + UBSan builds with GCC and Clang in GitHub Actions, with
  clang-format and opt-in clang-tidy.
- GoogleTest and CTest suites with seeded container comparisons, an independent search
  oracle, a golden corpus, stateful and metamorphic tests, and real CLI sessions.
- Allocation and I/O regressions, opt-in Clang libFuzzer targets, and GCC/gcov coverage reporting.

### Documentation

- Quick Start, CLI command reference and demo, and compilable C++ API examples.
- Architecture diagrams and decision records covering algorithms, public contracts, and complexity.
- [Roadmap](ROADMAP.md), [verification instructions](docs/TESTING.md), and publication checklist.

### Known limitations

- The index is local, append-only, and in memory. Documents and queries use ASCII.
- Ranked search scores every matching candidate for a positive limit. The CLI assumes trusted
  local files, and its input limits apply to individual commands and files.

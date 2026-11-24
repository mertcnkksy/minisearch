# Changelog

## Unreleased

- Added C++20 CMake targets for the core library and a CLI with help and development version output.
- Added Debug, Release, and sanitizer presets, plus GCC and Clang build and test jobs in GitHub Actions.
- Added GoogleTest, CTest CLI checks, clang-format, and optional clang-tidy integration.
- Added a contiguous DynamicArray with geometric growth, checked access, copy and move support,
  and coverage for allocation failures and seeded comparisons against `std::vector`.
- Documented the architecture, DynamicArray contract, and development roadmap.
- Added a generic open-addressed HashTable with linear probing, tombstone reuse, occupancy-based
  rebuilding, and deterministic tests against `std::unordered_map`.
- Added a DynamicArray-backed binary MinHeap with stateful comparators, linear-time bottom-up
  construction, and tests for allocation rollback and bounded Top-K selection.
- Added a stateless ASCII tokenizer with owned lowercase terms, zero-based positions, and byte
  offsets. Unsupported bytes report their offset and abort the call.
- Added an append-only positional inverted index with stable document and term IDs, corpus
  statistics, and logical rollback after failed document insertion.

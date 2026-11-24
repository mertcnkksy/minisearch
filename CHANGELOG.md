# Changelog

## Unreleased

- Added C++20 CMake targets for the core library and a CLI with help and development version output.
- Added Debug, Release, and sanitizer presets, plus GCC and Clang build and test jobs in GitHub Actions.
- Added GoogleTest, CTest CLI checks, clang-format, and optional clang-tidy integration.
- Added a contiguous DynamicArray with geometric growth, checked access, copy and move support,
  and coverage for allocation failures and seeded comparisons against `std::vector`.
- Documented the architecture, DynamicArray contract, and development roadmap.

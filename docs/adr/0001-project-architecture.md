# ADR 0001 — Project architecture

**Status** Accepted in MS-001

## Context

MiniSearch needs a testable C++20 core for learning and implementing data structures and
search algorithms, with room for multiple frontends.

## Decision

Use target-based CMake with a static `minisearch_core` and a small `minisearch` executable.
Keep public headers in `include/minisearch/` and compiled implementation in `src/`. Core
templates are defined in headers so they can be instantiated by consumers. The CLI depends
on the core through its public headers.

Use GoogleTest for unit tests and CTest for discovery and CLI integration checks. Pin the
GoogleTest source archive by version and SHA-256 and fetch it only when `BUILD_TESTING` is
enabled. CMake presets make Debug, Release, and sanitizer configurations repeatable. CI
compiles and tests each preset with GCC and Clang and separately runs formatting and
clang-tidy. Keep build diagnostics scoped to project targets.

## Consequences

Unit tests and later frontends share the same library target. The core and CLI build with
the C++ standard library alone. Enabling tests requires the pinned GoogleTest archive at
first configure. During MVP development the CLI reported `0.1.0-dev`. MS-010 sets the
CLI version to `0.1.0` using the CMake project version.

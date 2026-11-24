# Architecture

The C++20 core library exposes DynamicArray, HashTable, and MinHeap as public data structures. A minimal
command-line executable provides help and version output. The roadmap tracks later indexing,
query processing, and storage components.

| Path | Responsibility |
| --- | --- |
| `include/minisearch/` | Public core headers. `data_structures/` contains the header-only DynamicArray, HashTable, and MinHeap templates. |
| `src/` | Compiled core implementation and the CLI entry point. |
| `tests/` | GoogleTest unit tests and CTest CLI integration checks. |
| `benchmarks/` | Reserved for reproducible benchmark workloads and runners. |
| `examples/` | Reserved for standalone examples using project APIs. |
| `scripts/` | Development checks. |
| `docs/adr/` | Architecture decisions and public contracts. |
| `.github/workflows/` | Build, test, and static-check automation. |

`minisearch_core` owns the public include path and propagates the C++20 requirement to
consumers. It currently compiles version reporting and exposes the data structure headers.
`minisearch` links to the core and handles argument parsing and output.
MinHeap uses DynamicArray for owned contiguous storage and implements binary heap ordering
in its public header.
`minisearch_tests` consumes the same library target as the CLI. The build enables compiler
warnings and optional sanitizer and analysis flags on project targets.

See [ADR 0001](adr/0001-project-architecture.md) for the target boundaries,
[ADR 0002](adr/0002-dynamic-array.md) for DynamicArray,
[ADR 0003](adr/0003-open-addressing-hash-table.md) for HashTable, and
[ADR 0004](adr/0004-binary-min-heap.md) for MinHeap.

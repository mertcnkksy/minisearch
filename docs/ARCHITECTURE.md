# Architecture

The C++20 core library exposes DynamicArray, HashTable, and MinHeap as public data structures. A minimal
command-line executable provides help and version output. The core also includes a common ASCII
analysis step for future document indexing and query processing.

| Path | Responsibility |
| --- | --- |
| `include/minisearch/` | Public core headers. `data_structures/` contains the header-only DynamicArray, HashTable, and MinHeap templates. `text/` exposes the tokenizer API and its owned token type. |
| `src/` | Compiled core implementation and the CLI entry point. `text/` contains the ASCII scanner. |
| `tests/` | GoogleTest unit tests and CTest CLI integration checks. |
| `benchmarks/` | Reserved for reproducible benchmark workloads and runners. |
| `examples/` | Reserved for standalone examples using project APIs. |
| `scripts/` | Development checks. |
| `docs/adr/` | Architecture decisions and public contracts. |
| `.github/workflows/` | Build, test, and static-check automation. |

`minisearch_core` owns the public include path and propagates the C++20 requirement to
consumers. It compiles version reporting and the tokenizer and exposes the data structure headers.
`minisearch` links to the core and handles argument parsing and output.
MinHeap uses DynamicArray for owned contiguous storage and implements binary heap ordering
in its public header.
The tokenizer converts a borrowed text view into owned terms, positions, and byte offsets.
Future index and query modules can use the same tokenizer before consuming those terms.
The v0.2.0 Query Engine milestone plans UTF-8 validation, Unicode tokenization, ICU
integration, NFC normalization, and Turkish language support.
`minisearch_tests` consumes the same library target as the CLI. The build enables compiler
warnings and optional sanitizer and analysis flags on project targets.

See [ADR 0001](adr/0001-project-architecture.md) for the target boundaries,
[ADR 0002](adr/0002-dynamic-array.md) for DynamicArray,
[ADR 0003](adr/0003-open-addressing-hash-table.md) for HashTable,
[ADR 0004](adr/0004-binary-min-heap.md) for MinHeap, and
[ADR 0005](adr/0005-ascii-tokenizer.md) for the tokenizer.

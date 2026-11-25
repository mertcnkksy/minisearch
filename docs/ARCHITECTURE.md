# Architecture

The C++20 core library exposes DynamicArray, HashTable, and MinHeap as public data structures. The
command-line executable provides an interactive search session, help, and version output. The core includes a common ASCII
analysis step shared by the in-memory positional index and BM25 query scoring. Ranked retrieval
selects matches with a bounded heap.

| Path | Responsibility |
| --- | --- |
| `include/minisearch/` | Public headers for the data structures, tokenizer, positional index, BM25 scorer, ranked search, and stream-based CLI session. |
| `src/` | Compiled core implementation, interactive session modules under `cli/`, and the CLI entry point. |
| `tests/` | GoogleTest unit tests and CTest CLI integration checks. |
| `benchmarks/` | Reserved for reproducible benchmark workloads and runners. |
| `examples/` | Reserved for standalone examples using project APIs. |
| `scripts/` | Development checks. |
| `docs/adr/` | Architecture decisions and public contracts. |
| `.github/workflows/` | Build, test, and static-check automation. |

`minisearch_core` owns the public include path and propagates the C++20 requirement to
consumers. It compiles version reporting, the tokenizer, the positional index, BM25, and Top-K
retrieval.
`minisearch_cli` links to the core and owns bounded command and file reading, session state,
and output formatting. The `minisearch` executable handles startup arguments and passes its
standard streams to the session. The core has no terminal dependency.
MinHeap uses DynamicArray for owned contiguous storage and implements binary heap ordering
in its public header.
The tokenizer converts a borrowed text view into owned terms, positions, and byte offsets.
The index uses those terms to build ordered posting lists and corpus statistics. BM25
uses the same tokenizer for queries and scores the union of matching posting lists.
The BM25 scorer and ranked search share candidate accumulation. Raw BM25 results retain
document ID order. Ranked search consumes the candidates directly and returns up to the
requested number in rank order, using exact score and document ID comparisons.
The v0.2.0 Query Engine milestone plans UTF-8 validation, Unicode tokenization, ICU
integration, NFC normalization, and Turkish language support.
`minisearch_tests` exercises the core. `minisearch_cli_tests` injects streams and files into the
CLI modules. CTest also invokes the real executable with bounded input and timeouts. The
build enables compiler warnings and optional sanitizer and analysis flags on project targets.

Integration tests compare the core and CLI with an independent brute-force oracle.
Opt-in fuzzers link separate instrumented core and parser targets. The normal
executable and its libraries use the regular compiler configuration. See
[Testing](TESTING.md) for the test boundaries and verification commands.

See [ADR 0001](adr/0001-project-architecture.md) for the target boundaries,
[ADR 0002](adr/0002-dynamic-array.md) for DynamicArray,
[ADR 0003](adr/0003-open-addressing-hash-table.md) for HashTable,
[ADR 0004](adr/0004-binary-min-heap.md) for MinHeap,
[ADR 0005](adr/0005-ascii-tokenizer.md) for the tokenizer,
[ADR 0006](adr/0006-positional-inverted-index.md) for the positional index,
[ADR 0007](adr/0007-bm25-ranking.md) for scoring,
[ADR 0008](adr/0008-top-k-retrieval.md) for ranked retrieval, and
[ADR 0009](adr/0009-interactive-cli.md) for the CLI.

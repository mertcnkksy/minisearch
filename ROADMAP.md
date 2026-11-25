# Roadmap

| Milestone | Scope | Status |
| --- | --- | --- |
| v0.1.0 Foundation & MVP | C++20 project foundation and DynamicArray (MS-001), open-addressed HashTable (MS-002), binary MinHeap (MS-003), ASCII tokenizer (MS-004), positional inverted index (MS-005), core BM25 (MS-006), Top-K retrieval (MS-007), interactive CLI (MS-008), integration and fuzz testing (MS-009), and release preparation (MS-010). | MVP implemented. Review, hosted verification, and publication are tracked in MS-010B. |
| v0.2.0 Query Engine | Query parsing, Boolean and phrase search, TF-IDF, DAAT processing, UTF-8 validation, Unicode tokenization, ICU integration, NFC normalization, Turkish language support, and IR evaluation. Extends the existing positional index, BM25, and Top-K retrieval. | Planned |
| v0.3.0 Storage Engine | Delta and Variable Byte encoding, immutable segments, tombstones and merging, CRC32, and crash-safe storage. | Planned |
| v0.4.0 Concurrent Engine | Multithreaded indexing and an LRU cache. | Planned |
| v0.5.0 Advanced Search | Trie and Radix Trie, BK-tree and SymSpell, and English text processing. | Planned |
| v0.6.0 Performance Engineering | Reproducible benchmarks, Galloping Search, and MaxScore. | Planned |
| v0.7.0 API & Dashboard | Advanced interactive CLI features, a REST API using cpp-httplib, and a web interface with a performance analysis dashboard. | Planned |
| v1.0.0 Stable Release | Stabilize public interfaces, storage compatibility, and the release workflow. | Planned |

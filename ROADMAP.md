# Roadmap

| Milestone | Scope | Status |
| --- | --- | --- |
| v0.1.0 Foundation & MVP | MS-001 establishes the build, tests, CLI skeleton, and DynamicArray. MS-002 adds the Open Addressing Hash Table. MS-003 adds the Binary Min Heap. MS-004 adds the ASCII Document Tokenizer. MS-005 adds the Positional Inverted Index. MS-008 adds the MVP interactive indexing and search flow. | MS-001 through MS-005 and MS-008 implemented. MS-009 integration and release-readiness checks added. Remaining work planned. |
| v0.2.0 Query Engine | Positional index, Boolean and phrase search, BM25 and TF-IDF, DAAT processing, UTF-8 validation, Unicode tokenization, ICU integration, NFC normalization, and Turkish language support. | MS-006 core BM25 scoring and MS-007 Top-K retrieval implemented. Remaining work planned. |
| v0.3.0 Storage Engine | Delta and Variable Byte encoding, immutable segments, tombstones and merging, CRC32, and crash-safe storage. | Planned |
| v0.4.0 Concurrent Engine | Multithreaded indexing and an LRU cache. | Planned |
| v0.5.0 Advanced Search | Trie and Radix Trie, BK-tree and SymSpell, and English text processing. | Planned |
| v0.6.0 Performance Engineering | Reproducible benchmarks, Galloping Search, and MaxScore. | Planned |
| v0.7.0 API & Dashboard | Advanced interactive CLI features, a REST API using cpp-httplib, and a web interface with a performance analysis dashboard. | Planned |
| v1.0.0 Stable Release | Stabilize public interfaces, storage compatibility, and the release workflow. | Planned |

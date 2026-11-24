# ADR 0005 — ASCII tokenizer

**Status** Accepted in MS-004

## Context

MiniSearch needs a shared analysis step for document indexing and query processing. The
MVP needs term boundaries, stable positions, and byte offsets before it can build a
positional index or calculate term frequencies and document lengths.

## Decision

Start with an ASCII scanner with a small, deterministic contract. Reject unsupported
bytes explicitly so ingestion cannot introduce partial or unexpectedly transformed
terms. Use the same stateless tokenizer for documents and queries.

## Public API contract

`minisearch::Tokenizer::tokenize(std::string_view text)` returns a
`std::vector<minisearch::Token>` by value. The static entry point needs no configuration
or mutable state. The view accepts borrowed, length-delimited input without making a
full-text copy, so it handles embedded NUL bytes. The input memory must remain readable
and unmodified during the call. Empty input or input made entirely of separators
produces an empty result. Results retain source order and preserve duplicates.

## Token data model

| Field | Type | Meaning |
| --- | --- | --- |
| `term` | `std::string` | Owned, nonempty normalized ASCII letters and digits |
| `position` | `std::uint32_t` | Contiguous zero-based term order, equal to the result index |
| `start_offset` | `std::size_t` | Inclusive byte index in the original view |
| `end_offset` | `std::size_t` | Exclusive byte index in the original view |

A 32-bit position matches the integer width intended for posting lists. The largest
position is `UINT32_MAX`, allowing up to `UINT32_MAX + 1` tokens when the input and
container can hold them. The scanner checks the result index before converting it.
The private check is tested at the boundary directly, without constructing a multi-billion
element vector.

Offsets use the input view's size type. They are byte indices, and each interval satisfies
`start_offset < end_offset <= text.size()`. Intervals are sorted and disjoint.
Normalization preserves byte length, so each term is the lowercase form of its input
slice. The scanner increments its cursor only while it is less than the view's size
and subtracts a start index only after reaching or passing it.

## Tokenization and normalization

Each token is a maximal run of adjacent ASCII letters and digits. Classify bytes as
`unsigned char` and compare their numeric values.

| Byte range | Treatment |
| --- | --- |
| `0x30–0x39` | Preserve digits |
| `0x41–0x5A` | Lowercase by adding `0x20` |
| `0x61–0x7A` | Preserve lowercase letters |
| Other bytes through `0x7F` | Delimit runs |
| `0x80–0xFF` | Reject the call |

Separators include whitespace, punctuation, underscore, hyphen, apostrophe, control
bytes, NUL, and DEL. They never produce an empty term. For example, `C++20` produces
`c` and `20`, and `don't` produces `don` and `t`. Classification and case conversion
do not use locale facilities.

The cursor scans forward to find each run, checks the position, copies the run into
its final string buffer, and normalizes that buffer in place. The run boundary can be
examined by both the inner and outer scan. Only token bytes are revisited for copying
and normalization. There is no separate full-input validation or counting pass.

## Memory ownership and standard library usage

Each term owns its bytes, so the result survives destruction or mutation of the input
and remains independent of later calls. `DynamicArray` requires trivially copyable
elements. A `Token` owns a string, so `std::vector<Token>` supplies the required object
lifetimes and moves. The scanner moves the normalized string into the result. It
neither duplicates the input as a whole nor reserves one result slot per input byte.
Correctness never depends on a string's small-string optimization.

## Error handling and exception safety

- A non-ASCII byte causes `std::invalid_argument` with the message
  `Non-ASCII byte at offset N`, where `N` is the first high byte's zero-based decimal
  offset. This applies to UTF-8 BOMs, Turkish UTF-8 text, emoji, malformed UTF-8, and
  individual high bytes. The scanner classifies bytes and does not validate UTF-8.
- An unrepresentable position throws `std::length_error`. Standard string and vector
  capacity errors propagate as provided by the standard library. Allocation failures
  propagate as `std::bad_alloc`.
- Capacity or allocation failures can occur before a later high byte is examined or
  while its error message is being built. The API does not establish priority between
  these independent errors.
- All results are local until return. If any step throws, RAII destroys partial terms
  and the result vector. The input and earlier successful results remain intact, and a
  later call starts fresh. An isolated test executable injects failures at each observed
  allocation in a workload with long terms and result-vector growth.

## Thread safety

Independent calls can run concurrently on immutable input, including shared read-only
input. The caller must keep the view valid and synchronize any concurrent input writes.
Returned vectors and strings are independently owned and follow their ordinary
standard-library access rules.

## Time, space, and allocation behavior

Let `n` be the input byte count, `t` the result count, `L` the sum of normalized term
lengths, and `ell` the current term length. On valid input, `L <= n`.

| Work | Worst-case time | Amortized time | Space |
| --- | --- | --- | --- |
| Classify bytes and delimit runs | O(n) for a call | O(n) | O(1) cursor state |
| Copy and normalize term bytes | O(L) for a call | O(L) | O(L) owned output bytes |
| Position and offsets | O(1) per token | O(1) | O(t) output metadata |
| Build each term and append it | O(ell + t) for an append that relocates existing tokens | O(ell + 1) per token | One pending term and up to O(t) temporary vector slots at relocation |
| Successful tokenization end to end | O(n) for a call | O(n) across a call's appends | O(t + L) output storage |

The time bounds count byte operations and vector element moves and exclude allocator
internals. Amortized constant-time vector append bounds total relocation work by O(t).
The default-allocator string move is constant time. An implementation can store short
terms inline, but the analysis does not assume that it does. Outside result construction,
the scanner uses O(1) state. During growth both the old and new slot buffers can exist,
and the current string may own O(ell) bytes before those bytes transfer to the output.
Thus extra storage relative to the already committed output can peak at O(t + ell),
while total live storage for the call grows with its output. The vector uses its
standard-library growth policy and never reserves from the input's byte length.

## Known limitations and future Unicode support

The MVP covers ASCII only and splits technical names, e-mail addresses, URLs, and
decimal numbers at punctuation. The v0.2.0 Query Engine milestone plans UTF-8 validation,
Unicode tokenization, ICU integration, NFC normalization, and Turkish language support,
including Turkish I variants. That work can evaluate Unicode case folding. Later language
analysis can evaluate English text processing and stemming with Snowball. Changing analysis
rules can require rebuilding existing indexes.
Document and query analysis must stay aligned so query terms can match indexed terms.

## Future inverted index and BM25 integration

MS-005 can evaluate a `HashTable` from owned terms to `DynamicArray<std::uint32_t>` for
positions. Query and document calls produce the same normalized terms. Adjacent positions
can support phrase matching later. MS-006 can count positions per term to obtain term
frequency and use the total token count as document length. The current tests exercise
these inputs without adding index or ranking code to the core.

## Consequences

The tokenizer adds a compiled text-analysis module to `minisearch_core`. It needs only
the C++ standard library. Consumers get stable byte coordinates and explicit unsupported-
input errors. More expressive language analysis can extend the project in a later milestone.

# ADR 0006 — Positional inverted index

**Status** Accepted in MS-005

## Context

MiniSearch needs a mutable, in-memory index that provides term positions and the corpus
statistics used by future retrieval and ranking. The MVP accepts append-only ASCII
documents through the existing tokenizer.

## Decision and storage model

Use a `HashTable<std::string, TermId>` with explicit nonthrowing string equality for the
dictionary. Each ID addresses a record in a separate term vector. Storing a posting list
directly in a bucket would require the current HashTable to deep-copy that list during
rehash. A small ID keeps rehash costs independent of posting volume and gives the future
storage engine a stable logical identifier. Rehash still copies the owned term strings.

A term record holds a `std::vector<Posting>` and its collection frequency. A posting holds a
document ID and a `DynamicArray<Position>`. Positional values satisfy DynamicArray's
trivially copyable type requirements. The posting owns that array and needs a container
that supports its nontrivial lifetime. Document metadata is a vector of lengths indexed by
document ID. The index owns all terms, lengths, and positions. Source text and tokenizer
results can be destroyed after insertion.

`DocumentId`, `TermId`, and `Position` are unsigned 32-bit values. Document IDs are dense
and start at zero. Term IDs are assigned in first-occurrence order across committed
documents, independent of bucket order. `UINT32_MAX` is a valid ID in either domain. The
next ID is checked before narrowing. Counts, lengths, and frequencies use 64 bits to
represent all `UINT32_MAX + 1` IDs or positions when memory and container limits permit.
Document length is the number of tokens. Empty documents and documents containing only
separators receive IDs and contribute a length of zero.

The existing HashTable is neither copyable nor movable. InvertedIndex deliberately
shares those restrictions. Keeping storage and counters under one owner avoids deep
corpus copies and an extra ownership layer.

## Public API contract and statistics

| Operation | Behavior |
| --- | --- |
| `add_document(text)` | Tokenize and append an ASCII document, returning its ID. Invalid text, bounds errors, and allocation failures propagate. |
| `find_postings(term)` | Return a borrowed `std::span<const Posting>`, empty for an absent term. |
| `document_count()`, `unique_term_count()`, `total_token_count()` | Return the number of committed documents, distinct terms, and tokens. |
| `average_document_length()` | Return total tokens divided by document count as `double`. Return zero for an empty corpus. Include empty documents. |
| `document_length(id)` | Return the token count. Throw `std::out_of_range` for an unknown ID. |
| `document_frequency(term)` | Return the posting list size, or zero for an absent term. |
| `collection_frequency(term)` | Return the cached sum of term occurrences across documents, or zero for an absent term. |
| `term_frequency(term, id)` | Validate the ID first, then binary-search the sorted posting list. Return the matching position count or zero for an absent term or missing posting. |

Lookups compare exact normalized single terms. Empty, uppercase, punctuated, and multiword
inputs are misses. The tokenizer can normalize raw query text before these lookups. The
index accepts `const std::string&` so callers can pass a token's owned term directly to
the current HashTable. Converting a string view or a long literal into a string at the call
site may allocate and costs time and space linear in its length. Lookup itself creates
no string copy and adds no dictionary entry.

## Invariants and transactional insertion

Each posting list has strictly increasing document IDs. Every posting represents a single
term-document pair and stores a nonempty, strictly increasing set of token positions.
Across all terms, a document's positions cover its token range exactly once. TF comes from
the position count. DF comes from the number of postings. The sum of a term's TFs equals
its CF. The sum of corpus CFs equals the sum of document lengths and the total token count.

Insertion proceeds in four stages.

1. **Validation** checks the next document ID, tokenizes with the existing ASCII tokenizer,
   and checks length and total-count bounds. The index is untouched. Byte offsets are
   discarded here because the posting model uses tokenizer positions.
2. **Staging** groups tokens in a local HashTable keyed by string views into the immutable
   tokenizer result. The staging vector preserves first-seen order. Each position is
   appended once. A lookup per distinct term discovers existing IDs, and checked arithmetic
   determines new IDs and collection frequencies. The views die with the grouping table.
3. **Preparation** reserves geometric capacity for document lengths, term records, and
   affected existing posting lists. It also builds the one-posting vectors for new terms
   and reserves dictionary live capacity. These steps can allocate and can change
   capacities before a later failure. Dictionary reserve does not clear existing tombstones
   when no growth is needed, so subsequent insertions can still rebuild at the same capacity.
4. **Dictionary transaction and commit** first insert new owned keys. If a key copy or
   HashTable rebuild fails, the processed staging prefix identifies the keys to erase.
   The original strings are still owned by the tokenizer result. Erase uses nonthrowing
   hashing and equality, resets the bucket entry, and marks a tombstone. It allocates no
   storage. Once dictionary insertion succeeds, appending into reserved vectors moves
   nonthrowing records and publishes frequencies, document length, and corpus total.

The dictionary transaction can throw. Final publication is allocation-free with the
standard allocator because all vectors have been prepared and posting and term record
moves are nonthrowing. This preserves **logical** contents and the next IDs after any
failed insertion. Capacity increases and tombstones from preparation or rollback can
remain. A cluster of tombstones can increase probe costs until reused or rebuilt.
The allocation suite injects one-shot and persistent failures at observed allocation
points. It also exercises a same-capacity rebuild after repeated rollback.

## Reference invalidation and thread safety

Reacquire posting spans and references, including references into positions, after any
insertion attempt. A growing inner posting vector relocates posting objects. Growth of the
outer term vector can transfer inner buffers via nonthrowing vector moves, and dictionary
rehash relocates dictionary buckets independently. The contract does not require callers
to track which of these changes occurred. Preparation can invalidate views even when the
insertion fails. Destruction ends all borrowed view lifetimes.

Concurrent read-only calls are supported while the index remains alive and unchanged.
The caller must synchronize any writer with other operations and with users holding spans.

## Time and space complexity

Let `B` be the document byte length, `T` its token count, `U` its distinct term count,
`V` the vocabulary size, `D` the number of documents, `P` the posting count, `N` the
total token count, and `S` the total byte length of unique terms. For a lookup, `k` is
the term byte length, `C` is the dictionary bucket capacity, and `d` is the matching
posting list size. Bounds count byte operations, moves, and probes and exclude allocator
internals.

| Operation | Average time | Worst time | Amortized time | Auxiliary space |
| --- | --- | --- | --- | --- |
| Construction | O(1) | O(1) | O(1) | O(1) |
| `document_length`, corpus count and average queries | O(1) | O(1) | O(1) | O(1) |
| `find_postings`, DF, CF | O(k + 1) | O((k + 1) × C) | Same as a lookup | O(1) |
| TF | O(k + 1 + log(d + 1)) | O((k + 1) × C + log(d + 1)) | Same as a lookup | O(1) |
| `add_document` | O(B + T + U) without corpus growth | See growth and collision costs below | Geometric vector relocation plus tokenization, hashing, and rehash costs below | O(B + T + U) staging plus overlapping buffers during growth |

Tokenization is O(B). Local grouping uses expected constant hash probes plus the
cost of hashing the token bytes. Position-array appends are amortized constant time,
including geometric growth. For a successful history, total metadata and posting-list
relocation work is linear in its aggregate append counts. A single growing insert can
move O(D) lengths, O(V) term records, and the existing postings of each touched list
that grows. This can be O(P) posting moves in one insertion. These moves transfer
owned buffers instead of copying their positions.

With poor hash distribution, staging can use O(U × (B + T)) work, and dictionary
lookups, insertions, and rollback can scan O(C) buckets per key. Rehash can scan old
and new bucket buffers, take O((V + U)²) probes in the worst case, and copy up to
O(S + B) key bytes. Geometric HashTable growth amortizes bucket relocation under good
hash distribution. The bytes copied at each actual rebuild still count, especially for
long keys. Repeated failures can add same-capacity rebuild costs. There is no
unconditional O(T) insertion bound.

Persistent storage grows as O(D + V + P + N + S) with geometric spare capacity over
successful appends. Retained capacity can also reflect a failed preparation's high-water
mark. Physical costs include allocated hash buckets, term string buffers, vector slots,
and position slots. Growth can briefly keep old and new storage alive together,
including duplicated keys during a dictionary rebuild. Staging and rollback do not
make a full copy of the corpus. Destruction releases owned buffers in time proportional
to the allocated buckets and constructed records.

## Known limitations and future integration

The index is append-only and in memory. It delegates ASCII normalization and rejection
to the tokenizer. UTF-8 validation, Unicode tokenization, and Turkish language support
remain in v0.2.0 Query Engine. This index does not score, intersect, or match phrases.

MS-006 can use TF, DF, document length, corpus size, and average length for BM25.
Sorted, immutable-through-the-API postings support later Top-K and query modules.
Stable IDs and ascending positions also support v0.3.0 storage layouts and future
delta encoding.

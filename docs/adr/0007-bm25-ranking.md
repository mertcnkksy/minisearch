# ADR 0007 — BM25 ranking

**Status** Accepted in MS-006

## Context and decision

The positional index can supply term frequencies, document frequencies, document lengths,
and corpus totals. Add a read-only Okapi BM25 scorer with positive IDF. It scores a single
document or all sparse matches for an ASCII query. Results are raw relevance scores,
rather than probabilities or normalized percentages. Comparing scores across different
queries generally has little meaning.

## Mathematical model and parameters

For every unique normalized query term, add the following contribution.

```text
IDF = ln(1 + (N - DF + 0.5) / (DF + 0.5))
norm = (1 - b) + b * DL / AvgDL
TF factor = TF * (k1 + 1) / (TF + k1 * norm)
contribution = IDF * TF factor
```

`N` is the number of documents, `DF` is the posting list size, `TF` is the number of
positions in a posting, `DL` is the document length in tokens, and `AvgDL` is the corpus
average length. Default parameters are `k1 = 1.2` and `b = 0.75`. The scorer accepts any
finite `k1 >= 0` and finite `b` in `[0, 1]`. It throws `std::invalid_argument` for other
values. The positive IDF and defaults are also described by Lucene. MiniSearch uses
its own token lengths and query policy. [Lucene BM25Similarity documentation](https://lucene.apache.org/core/10_3_1/core/org/apache/lucene/search/similarities/BM25Similarity.html)

## Floating-point safety

Use `double` for scoring. Check `DF <= N` before subtracting unsigned counts. Compute
`N - DF` as an integer before conversion and use `std::log1p` to retain small IDFs for
very frequent terms. The private math helpers accept 64-bit frequencies and lengths.
A zero TF contributes zero before any division. When `k1 = 0`, a match has TF factor one.
For `0 < k1 <= 1`, evaluate the original factor. For `k1 > 1`, divide numerator and
denominator by `k1` before multiplying. This avoids forming `TF * (k1 + 1)` or
`k1 * norm` with a potentially huge parameter.

```text
TF factor for k1 > 1 = TF * (1 + 1 / k1) / (TF / k1 + norm)
```

A matching document has positive length and positive average length. The normalization is
a convex combination of one and `DL / AvgDL`, so it is positive. The TF factor is between
one and `TF / norm`. Since `TF <= DL`, `TF / norm` is at most
`max(DL, AvgDL)`. In the positional index, lengths, numbers of query tokens, and
document counts are bounded by `2^32`, and IDF is less than 24. The sum is far below
the largest representable double, even under the loose bound obtained by multiplying
those limits. At extreme `k1`, a small scaled term can round away without changing the
dominant contribution. The scorer requires ordinary floating-point semantics and does
not clamp scores or use fast-math. Same-environment query order is deterministic.
Bitwise identity across compilers, math libraries, platforms, or floating-point modes
is outside the contract.

## Public API, query semantics, and index access

- Construct `BM25Scorer` with a `BM25Config` or use the defaults. The configuration is
  stored by value. The index is borrowed as a const reference for each call.
- `score_document(index, id, query)` validates the document ID before tokenizing.
  Thus an unknown ID throws `std::out_of_range` even with an invalid or empty query.
  A valid document without a match scores zero.
- `score_query(index, query)` returns a `DynamicArray<ScoredDocument>` ordered by
  ascending document ID. Scores and IDs are owned values and survive the index.
- Both calls use the tokenizer. ASCII errors propagate. After tokenization, sort
  by normalized term and remove duplicates in place. Lexicographic order fixes
  contribution order. Repeated terms carry no additional query weight.
- Fetch document count and average length afresh. Empty documents count toward
  both statistics and have zero score. Empty, separator-only, or unknown-term
  queries return no matches. With an empty corpus or a zero average, a valid query
  produces no matches. Tokenization still validates the query.

## Sparse candidate generation and scoring

Fetch each term's posting span once and skip empty lists. For a single document,
binary-search the span by document ID, using the same lookup convention as
`InvertedIndex::term_frequency`. This also leaves DF available from the same lookup.
For the query result, compute IDF once per matching term and traverse only its
postings. Read TF from the position array size and retrieve length by document ID.
The result covers the union of matching lists.

The existing HashTable has no iterator API. Use `HashTable<DocumentId, std::size_t>`
to map candidate IDs to row indices in a `DynamicArray<ScoredDocument>`. On first
encounter, append a row. On later encounters, add to that row. Indices survive
reallocation of the score array, and pointers returned by HashTable are used before
any mutating operation. Sort the accumulated array by document ID after row indices
are no longer needed. No array is sized by the corpus count. The scorer holds no
postings or statistics between calls.

## Exception safety and thread safety

Tokenizer, configuration, index ID, and allocation errors propagate. Temporary
query data, row maps, and results are local to the call. Errors leave the index and
earlier returned scores intact. The isolated allocation suite sweeps observed
allocation sites during tokenization, map growth and rehash, and result growth.
Term sorting and deduplication reuse the token vector.

Independent scoring calls can read the same unchanged index, with the scorer and index
kept alive. The caller must synchronize writes with scoring. An attempted document
insertion can invalidate the posting spans, including when that attempt fails.

## Time and space complexity

Let `B` be query bytes, `T` token count, `U` unique terms, `K` the largest term length,
`N` corpus size, `M` the sum of relevant posting list lengths, and `C` the number of
matching documents. Costs exclude allocator internals.

| Work | Cost |
| --- | --- |
| Tokenization | O(B) |
| Lexicographic sorting and deduplication | O(T log(T + 1)) term comparisons, each costing up to O(K) |
| Corpus statistics and term lookup | O(1) corpus statistics and U dictionary lookups, including term hashing and probes |
| Single document | At most one binary search per nonempty list, costing O(log(DF + 1)) each |
| Query accumulation | O(M) expected with well-distributed IDs and geometric growth. Pathological collisions can cost O(M × C) probes, including rehash work. |
| Result order | O(C log(C + 1)) after accumulation |

Term hashing costs linear time in the key length. Index lookups can additionally probe
the full bucket array in the worst case. Candidate IDs and row indices have fixed size.
Query data uses O(B + T) temporary space and the map uses O(C) buckets. The output uses
O(C) space, and rehash and array growth can briefly keep old and new buffers. Single
document scoring uses O(B + T) temporary space and no candidate map. There is no O(N)
initialization or scan for a sparse query.

## Known limitations and Top-K integration

ASCII processing follows the tokenizer. Unicode and Turkish analysis remain in v0.2.0
Query Engine. The scorer supplies raw scores and document IDs in ID order. BM25 treats
query terms as a set and uses no position or phrase bonuses. There is no score cache.

`ScoredDocument` is trivially copyable, default-constructible, and nonthrowing to move.
MS-006 established heap compatibility. MS-007 uses the bounded heap to place the
worst selected result at its root, with lower scores first and larger document IDs
first on ties. See [ADR 0008](0008-top-k-retrieval.md) for the ranked search contract.

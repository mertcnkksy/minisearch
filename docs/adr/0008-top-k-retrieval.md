# ADR 0008 — Top-K retrieval

- Status: Accepted

## Context and decision

The positional index and BM25 scorer provide one score per matching document. Interactive
consumers need a limited list in relevance order. Use the existing binary MinHeap to select
the best matches and expose a single search function in the core library. Keep selection
and candidate accumulation internal.

## Public search API

```cpp
[[nodiscard]] DynamicArray<ScoredDocument> search(const InvertedIndex &index,
                                                  std::string_view query, std::size_t k,
                                                  BM25Config config = {});
```

`minisearch/search/search_engine.hpp` exposes the function. It borrows the index and query
for the duration of the call. Default parameters are `k1 = 1.2` and `b = 0.75`. The same
validator as `BM25Scorer` requires finite `k1 >= 0` and finite `b` in `[0, 1]`.

## Ranking contract and exact ties

Higher scores are better. Exactly equal scores are ordered by ascending document ID.
Positive and negative zero are equal for ranking. Adjacent finite scores remain distinct
and the full unsigned document ID range is supported.

In MinHeap, `compare(a, b)` means that `a` belongs closer to the root. The search comparator
places lower scores first and, on exact ties, larger document IDs first. This is a
nonthrowing lexicographic strict weak order over finite, nonnegative scores. The same
comparator decides whether an incoming candidate should replace the root. The selector
requires valid scores and unique document IDs. Candidate accumulation supplies both under
the BM25 and index contracts.

## Bounded heap selection and limits

Compute `H = min(k, candidate_count)` before reserving any selection storage. Return
immediately for zero. Reserve exactly `H` slots in the heap and fill them with the first
`H` candidates. When full, replace the root with an incoming candidate only if that
candidate ranks higher. A pop before each replacement push keeps the size at most `H`
and reuses the reserved storage.

At each step the heap contains the best `min(H, processed_count)` candidates. Before it is
full all processed candidates fit. Once it is full the worst selected candidate is at the
root. An incoming candidate that ranks higher displaces that root. Any other candidate
leaves the selected set unchanged. Extract the heap into an output array reserved for
`H` results, then reverse the array to obtain descending rank.

The selector copies each chosen score without recalculating it. An empty candidate set
returns an empty array. A limit of one returns the best single match when it exists.
Limits at or above the candidate count, including `SIZE_MAX`, return all matches.
`std::size_t` is the API limit type. Callers that parse signed or textual limits must
reject negative values before conversion.

## BM25 integration, candidate uniqueness, and query semantics

The scorer and search use the same internal accumulation function. It tokenizes and
deduplicates the query, then adds the unchanged BM25 contributions in lexicographic
term order. It reads current corpus statistics from the index, fetches each term's
postings once, and computes IDF once per nonempty list. Unknown terms are skipped.
The existing HashTable maps document IDs to indices in the candidate DynamicArray.
Multiple matches for the same document accumulate in that row. The selector borrows the
resulting array and visits each candidate once. `score_query` still sorts the array by
document ID. Only the ranked search path skips that sort.

Empty and separator-only queries yield no matches. ASCII validation applies even to an
empty corpus and to a zero result limit. Configuration is checked first, so an invalid
configuration takes precedence over an invalid query. At zero limit, the tokenizer
validates the query once and the function returns before any dictionary lookup, scoring,
or heap construction.

## Ownership, exception safety, and thread safety

The returned DynamicArray owns its ScoredDocument values independently of the query,
index, candidate array, heap, and other search results. Search does not modify the
index. The candidate map holds row indices, which survive growth of the score array.
Pointers to map values are consumed before mutation and heap root references are used
before the next heap operation.

Configuration, tokenization, capacity, and allocation errors propagate. Temporary
objects are local and destroyed on failure. The index and earlier results remain valid,
and a failed search publishes no partial result. Independent calls can read the same
unchanged index concurrently. The caller must synchronize searches with any index
mutation, including an insertion that eventually fails.

## Selection and end-to-end complexity

Let `B` be query bytes, `T` token count, `U` unique term count, `L` the longest query
term length, `M` the total lengths of the relevant posting lists, `C` the number of
unique matched documents, and `H = min(k, C)`. Costs exclude allocator internals.

| Stage | Time and space |
| --- | --- |
| Tokenization | O(B) time and O(B + T) temporary space. A zero-limit search stops after validation and this stage. |
| Sort and deduplicate query terms | O(T log(T + 1)) comparisons, each costing up to O(L). Reuses the token vector. |
| Corpus statistics and term dictionary | O(1) statistics access and U dictionary lookups. Hashing costs O(B) in aggregate and well-distributed hashes yield O(U) expected probes. Each lookup can probe the index's full bucket array in the worst case. String equality checks cost up to O(L) each. |
| Posting traversal and score accumulation | O(M) expected, including row lookup and insertion. One contribution per posting and one IDF per nonempty list. Pathological row-map collisions can cost O(M × C) probes including rehash. |
| Row-map growth and candidate materialization | Under well-distributed hashes, geometrically growing the map takes O(C) total rehash work. Rehashing can take O(C²) total probes with pathological collisions. The DynamicArray performs O(C) total initialization and copying during geometric growth. Map and candidate storage each use O(C) space, including buffers temporarily held during growth. |
| Document ID ordering | Ranked search skips this stage. Raw `score_query` costs O(C log(C + 1)) here. |
| Reserve and bounded selection | O(H) initialization and O(C log(H + 1)) worst-case selection for H ≥ 1. Selection is O(C) at H = 1 and O(1) at H = 0. Heap storage is O(H). |
| Extract and reverse | O(H log(H + 1)) to pop the heap and O(H) to reverse its extracted order. Output uses O(H) space. |

For a positive limit, expected end-to-end time is O(B + L × T log(T + 1) + U + M +
C log(H + 1)), with the dictionary and row-map collision costs above in the worst case.
The total peak additional space is O(B + T + C + H), including candidates and output.
Work depends on matching postings and candidates. There is no corpus-sized score buffer
or corpus scan. With a small limit the selector avoids sorting the full candidate array.

## Limitations and future query processing

Ranked retrieval applies BM25 to an in-memory ASCII index. It materializes scores for
all matching documents and offers no score-based posting pruning. Unicode tokenization,
UTF-8 validation, ICU, NFC normalization, and Turkish support remain in v0.2.0 Query
Engine. Future DAAT, WAND, and MaxScore work can change candidate generation while
preserving the public result order and exact tie rule.

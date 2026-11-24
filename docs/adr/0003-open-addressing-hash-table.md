# ADR 0003 — Open addressing hash table

**Status** Accepted in MS-002

## Context

MiniSearch will need to map terms to IDs and index metadata. MS-002 adds an in-memory
dictionary with explicit collision handling and predictable occupancy limits.

## Decision

`minisearch::HashTable<Key, Value, Hash, KeyEqual>` owns a contiguous array of buckets.
Each bucket is never used, occupied, or marked deleted (a tombstone). An occupied bucket
contains its key, value, and cached hash inline. Allocation is deferred until the first
successful insertion or a nonzero reserve. Capacities are powers of two starting at eight.

Linear probing starts at `hash & (capacity - 1)` and wraps at the end of the array. Lookups
stop at a matching entry or a never-used bucket. Deletion destroys the entry and leaves
a tombstone so lookups can still reach later entries. Insertion remembers the first
tombstone and uses it only after checking for an equivalent key farther along the chain.

An insertion grows the table when it would take the live load above 75%. A separate 87.5%
limit counts both live entries and tombstones. When a new entry would exceed that limit
by occupying a never-used bucket, the table rebuilds at the same capacity if the live
entries still fit. The gap between the two limits leaves room for churn and ensures
probes always encounter a never-used bucket on a miss. Rehashing copies live entries
into fresh storage, places them using their cached hashes, and drops tombstones.
The replacement becomes visible only after all required entry constructions succeed.

## Public contract

- `Key` and `Value` must be copy-constructible object types. Neither needs a default
  constructor or assignment operator. Copyable entries let rehashing preserve the old
  table until the replacement is ready. The table itself has a single owner and cannot
  be copied or moved.
- `Hash` and `KeyEqual` default to `std::hash<Key>` and `std::equal_to<Key>`. You can
  supply custom policies at construction. They must be callable as const objects.
  Equality must be an equivalence relation, and equivalent keys must produce the same
  hash. Hash and equality results must remain stable for stored keys. Distribute hash
  values across the low bits for good performance with power-of-two capacities.
- `insert(key, value)` takes its arguments by value and returns whether it inserted a
  new key. An equivalent key keeps the stored value. `find(key)` returns a pointer to
  the stored value or `nullptr` on a miss. Const lookup returns a const pointer.
  `contains(key)` reports presence. `erase(key)` returns whether it removed an entry.
- `reserve(count)` takes a live entry count and grows only when that count would
  exceed the live limit. Later insertions can still rebuild to clear tombstones.
  Erasure and `clear()` retain capacity. `clear()` destroys live entries and removes
  tombstones. `load_factor()` reports live entries divided by capacity, or zero at
  zero capacity. `max_load_factor()` is always 0.75.
- A successful rebuild, a growing reserve, or `clear()` invalidates all value pointers.
  Erasing a key invalidates its value pointer. Other successful insertions and erasures,
  lookups, duplicate insertions, and no-op reserves preserve pointers to surviving entries.
- Impossible capacity requests throw `std::length_error`. Allocation, entry construction,
  and hash or equality exceptions propagate. Failed insertions and reserves preserve the
  table's contents, capacity, and existing value pointers. If a hash or equality policy
  throws during erasure, nothing is erased. `clear()` and the size and load queries are
  nonthrowing. Policy side effects and changes to arguments moved by the caller are outside
  the table's guarantees.

## Complexity

Let `C` be the old capacity, `C'` the replacement capacity, and `n` the number of
live entries. The table assumes constant-time hashing, equality, and entry operations
for the selected types. Expected times assume well-distributed hashes. Space counts
bucket storage separately from any allocations owned by keys, values, or policies.

| Operation | Expected time | Worst-case time | Extra bucket space |
| --- | --- | --- | --- |
| Construction, size and load queries | O(1) | O(1) | O(1) |
| `find`, `contains`, `erase`, or `insert` without rebuilding | O(1) | O(C) | O(1) |
| Rebuilding `insert` or growing `reserve` | O(C + C') | O(C + C' + n²) | O(C') |
| No-op `reserve` | O(1) | O(1) | O(1) |
| `clear`, destruction | O(C) | O(C) | O(1) |

The table retains O(C) buckets. With automatic growth, insertion is expected amortized
O(1) over a sequence starting from an empty table. The geometric growth and the gap
between the occupancy limits bound rebuild frequency. Under severe clustering, a
single probe can traverse O(C) buckets and rehashing can perform O(n²) probing work.

## Consequences

Inline buckets avoid a separate allocation per entry. Linear probing keeps consecutive
probes close together but can form clusters. Rebuilding temporarily allocates a second
bucket array and copies the live entries. Callers must synchronize access if a thread
can mutate a table that other threads use.

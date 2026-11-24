# ADR 0002 — DynamicArray

**Status** Accepted in MS-001

## Context

Posting lists will need a growable, contiguous sequence of document IDs. MS-001 keeps the
element and lifetime model small enough to reason about directly.

## Decision

`minisearch::DynamicArray<T>` supports non-cv object types that satisfy
`std::is_trivially_copyable_v<T>` and `std::is_default_constructible_v<T>`. For example,
`std::uint32_t` and over-aligned trivially copyable records are supported. Containers such
as `std::string` and element types with nontrivial destruction require a different lifetime
model.

A `std::unique_ptr<T[]>` owns a contiguous array of `capacity()` value-initialized objects.
The first `size()` objects form the logical sequence. Bytewise copying of trivially copyable
elements avoids requiring copy assignment and lets reallocation preserve the sequence.
`push_back(value)` requires a reference to a live `T` that is not a potentially overlapping
subobject, such as a base class or a member declared with `[[no_unique_address]]`. Appending
an existing element is valid even when the buffer grows.

The first insertion allocates one slot. Insertion doubles a full capacity and saturates at
the smaller of `SIZE_MAX / sizeof(T)` and `PTRDIFF_MAX`, which bounds byte counts and
iterator differences. `reserve(k)` allocates exactly `k` slots when it grows the buffer.
Copies own independent storage with capacity equal to the source size. Moves transfer the
allocation and leave the source empty with zero capacity. Self-copy and self-move are no-ops.
`clear()` and `pop_back()` change logical size and keep the allocation. Removed values are
not wiped.

## Preconditions, exceptions, and iterator invalidation

- `operator[]` requires `index < size()`. `at()` checks this condition and throws
  `std::out_of_range` otherwise. Both have mutable and const overloads.
- `reserve()` throws `std::length_error` for requests above the capacity limit. A full
  array at the limit rejects further insertion with `std::length_error`. The allocator can
  reject smaller requests, for example with `std::bad_alloc`.
- Growing `push_back()`, growing `reserve()`, and copy operations propagate allocation and
  element default-construction exceptions. Existing arrays retain their state on failure.
  Default construction, destruction, move operations, queries, unchecked access, iteration,
  `clear()`, and `pop_back()` do not throw when their preconditions hold.
- `begin()` and `end()` are contiguous pointer iterators over `[begin(), end())`. An empty
  range has equal endpoints. Dereference requires an element in range.
- Reallocation invalidates all iterators, pointers and references into the previous buffer.
  `reserve()` without growth invalidates none. A `push_back()` without growth invalidates
  the previous end. `pop_back()` invalidates the removed element and previous end. It is a
  no-op on an empty array. `clear()` invalidates all logical element references and iterators.
- Non-self copy and move assignment invalidate iterators and references into the old
  destination. A move preserves pointers and iterators to source elements, which then refer
  to the destination's buffer. Obtain new endpoint iterators from the current owner.

## Complexity

Let `n` be the current size, `c` the current capacity, `m` the source size for a copy,
and `k` the requested reserve capacity. Auxiliary space includes newly allocated buffers
while old allocations may still be live. The table treats `T` as fixed-size, assumes
constant-time default construction per element, and excludes allocator internals.

| Operation | Time | Auxiliary space |
| --- | --- | --- |
| Default construction | O(1) | O(1) |
| Destruction | O(1) element work | O(1) |
| Copy construction or non-self copy assignment | O(m) for a nonempty source, O(1) when empty | O(m) slots plus O(1) bookkeeping |
| Move construction, move assignment, or self-assignment | O(1) | O(1) |
| `push_back()` without growth | O(1) | O(1) |
| `push_back()` with growth | O(n) for a nonempty array, O(1) for the first element | O(n) slots for a nonempty array, O(1) for the first element |
| Repeated `push_back()` with automatic growth | Amortized O(1) per insertion | As above for each insertion |
| `reserve(k)` with `k > c` | O(k) | O(k) slots |
| `reserve(k)` with `k <= c` | O(1) | O(1) |
| `pop_back()`, `clear()` | O(1) | O(1) |
| `size()`, `capacity()`, `empty()` | O(1) | O(1) |
| `operator[]`, `at()` | O(1) | O(1) |
| `begin()`, `end()` | O(1) | O(1) |

Retained storage is O(c) slots and O(1) metadata. During a successful growing reserve,
up to `c + k` slots coexist. Geometric growth gives an amortized bound for automatic
insertion. Explicit reserve calls can add their own reallocation costs.

## Consequences

Value-initializing capacity simplifies ownership and cleanup but can initialize unused
slots. Supporting general nontrivial element types would require construction and
destruction tailored to the live range. Mutation is single-threaded unless callers
synchronize access.

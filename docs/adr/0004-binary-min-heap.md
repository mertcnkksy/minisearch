# ADR 0004 — Binary min heap

**Status** Accepted in MS-003

## Context

MiniSearch will need to retain the best K scored documents without sorting every candidate.
A min heap provides constant-time access to the current selection threshold and logarithmic
replacement work when a better candidate arrives.

## Decision

Implement `minisearch::MinHeap<T, Compare>` as a binary heap over the existing `DynamicArray<T>`.
The heap controls element order. DynamicArray owns the allocation and provides geometric
growth, checked capacity arithmetic, alias-safe append, and RAII cleanup.

## Public API contract

| API | Behavior |
| --- | --- |
| `MinHeap()`, `MinHeap(compare)` | Start empty with the default or supplied ordering policy. |
| `MinHeap(std::span<const T>, compare)` | Copy the input into independent storage and heapify it. The comparator argument is optional when it can be default-constructed. |
| `push(const T&)` | Copy a value into the heap. The argument may be `top()`, even if the push grows storage. |
| `pop()` | Remove the root. An empty heap is unchanged. Capacity is retained. |
| `top() const` | Return a const reference to the root. Throw `std::out_of_range` when empty. |
| `size()`, `capacity()`, `empty()` | Query the logical element count, allocated slots, and emptiness. |
| `reserve(k)`, `clear()` | Reserve at least `k` slots or remove every logical element. Clear retains capacity. |
| Copy, move, destruction | Copies own independent storage. Moves transfer storage. Destruction releases storage. |

Copy construction and non-self copy assignment use a capacity equal to the source size,
including zero for an empty source. Moves preserve the source capacity in the destination.
A moved-from heap is empty with zero capacity and retains its ordering policy, so callers
can push new elements into it. Self-copy and self-move assignment preserve the existing heap.

## Binary heap architecture

At zero-based index `i`, the parent for a nonroot node is `(i - 1) / 2`. Its children are
at `2 * i + 1` and `2 * i + 2` when present. For every child, `compare(child, parent)`
is false. The root is therefore an element of highest priority under the chosen comparator.

Insertion appends and sifts the new element up while it precedes its parent. Removal places
the last element at the root and sifts it down by swapping with the preceding child as
needed. Sift-down computes child indices only when `i < size() / 2`, which bounds the
arithmetic and includes the case with one left child. Equivalent elements can share the heap.

The span constructor reserves once, appends the input values without sifting, then applies
Floyd's bottom-up algorithm. It sifts each internal node down in reverse index order, so
each child's subtree is already a heap when its parent is processed.

For `n` input elements, a node of height `h` can sift through at most `h` levels, with up to
two comparisons per level. For each positive integer `j`, exactly `floor(n / 2^j)` nodes
have height at least `j`. Summing the heights gives
`floor(n/2) + floor(n/4) + ... = n - popcount(n)`, as each set bit at position `b`
contributes `2^b - 1` to the sum. Hence heapify needs at most `2 * (n - popcount(n))`
comparisons. Copying the input also takes linear time. Tests count comparator invocations
for descending inputs with multiple sizes, including powers of two and adjacent sizes.

## DynamicArray integration and type requirements

Elements meet the DynamicArray requirements for unqualified, trivially copyable,
default-constructible object types. The heap also requires nonthrowing move construction
and move assignment to exchange elements in place. Scalars and simple scored records
satisfy these requirements. Owning types such as `std::string` and records with const members
that prevent assignment are outside this API. Values copied into storage must refer to
live elements outside potentially overlapping subobjects, as required by DynamicArray.

## Comparator requirements

`compare(a, b)` returns `true` when `a` should be above `b`. The default is
`std::ranges::less`, which makes the minimum the root for ordinary numeric inputs. A
stateful comparator can be passed to either constructor. For comparison tests,
`std::priority_queue` has the opposite convention, so an integer min heap is compared
against a priority queue using `std::greater<int>`.

The comparator must implement a stable strict weak ordering on admitted values. A
const call on two const element references must return `bool` and be declared nonthrowing.
Copy construction, copy assignment, and destruction of the comparator must also be
nonthrowing. A copy must preserve the ordering and leave the source ordering intact.
Changing externally shared comparator state in a way that changes ordering breaks the
heap's precondition. Comparator move operations are unused. Heap moves copy the
comparator and transfer the array so both heaps retain usable ordering policies.

When comparing floating-point values with ordinary relational operators, inputs must
exclude NaNs. The generic heap leaves validation to the caller.

## Exception safety

- Default construction may throw if default construction of the comparator throws.
  Construction with a supplied comparator and no elements is nonthrowing. The span
  constructor and copy constructor may fail during allocation or initialization of slots.
  Failed construction leaves no heap object or owned allocation.
- `push`, `reserve`, and copy assignment provide the strong guarantee for heap state,
  capacity, ordering, and existing references. DynamicArray handles allocation,
  capacity overflow, and slot construction before replacing storage. Copy assignment
  copies storage before assigning the comparator. Sifting and comparator assignment
  are nonthrowing. External side effects of user-defined operations are outside this guarantee.
- `pop`, `clear`, size queries, moves, and destruction are nonthrowing for conforming types
  and comparators. `top` changes no heap state. If empty, it throws `std::out_of_range`.
- Comparator invocation or copying that can throw is rejected by the template constraints.
  This lets sifting operate in place without a rollback buffer or a partially reordered heap.

## Reference invalidation

Reallocation, `clear()`, destruction, and successful non-self assignment invalidate the
old destination's root reference. Reserve without growth and failed mutations preserve
references. A move transfers the source buffer and any root reference into the destination.
Popping the last element invalidates its reference. A push without reallocation or a pop
with remaining elements can change the value in the root slot. Obtain a fresh `top()`
after either operation to observe the current root.

## Time and space complexity

`n` is the current size or the number of input elements, `m` is the source size for a
copy, and `k` is projected capacity for a growing reserve. The table treats element size,
comparing values, initializing or moving one slot, and comparator operations as constant
cost. It excludes allocator internals. Bounds use `n + 1` or `m + 1` to include empty cases.

| Operation | Worst-case time | Amortized time | Auxiliary space |
| --- | --- | --- | --- |
| Empty construction or destruction | O(1) | O(1) | O(1) |
| Span construction | O(n + 1) | O(n + 1) | O(1) besides the O(n) result buffer |
| Copy construction | O(m + 1) | O(m + 1) | O(1) besides the O(m) result buffer |
| Non-self copy assignment | O(m + 1) | O(m + 1) | O(m + 1) while old storage remains live |
| Move construction, move assignment, self-assignment | O(1) | O(1) | O(1) |
| `push`, without growth | O(log(n + 1) + 1) | O(log(n + 1) + 1) | O(1) |
| `push`, including automatic growth | O(n + 1) | O(log(n + 1) + 1) | O(n + 1) when growing |
| `pop`, `sift_up`, `sift_down` | O(log(n + 1) + 1) | O(log(n + 1) + 1) | O(1) |
| `top`, size queries, `clear`, reserve without growth | O(1) | O(1) | O(1) |
| `reserve(k)` with growth | O(k) | O(k) | O(k) |

Persistent storage is O(capacity) element slots, a comparator, and constant-size metadata.
The old and new buffers can coexist during a reallocation or copy assignment. Geometric
growth amortizes the array relocation cost across pushes. Explicit reserves carry their
own costs. Comparator cost multiplies the number of comparisons. Slot construction, moves,
and comparator copies contribute their own costs if the type makes them nonconstant.

## Consequences and known limitations

The single contiguous allocation avoids per-node allocation, and the nonthrowing ordering
requirements keep modification code simple. Spare slots are value-initialized through
DynamicArray, so a large reserve can initialize slots before they are used. Equivalent
elements have no stable insertion order. Bulk construction creates a new heap. Callers
must synchronize access when a thread can mutate a heap shared with other threads.

## Future Top-K integration

For scored documents, a higher score is better. Equal scores favor the smaller document ID.
The worst-first comparator therefore orders lower scores before higher scores and larger IDs
before smaller IDs on a tie. Scores must be non-NaN. With K greater than zero, fill the heap
to K and replace its root only when the candidate ranks higher. This keeps the worst selected
document at the root and bounds persistent storage by O(K) slots when capacity is reserved
for K entries. Popping and reversing produces best-first output. MS-003 tests this pattern
with scored records and a reference sort.

#include "top_k.hpp"

#include "minisearch/data_structures/min_heap.hpp"

#include <algorithm>

namespace minisearch::detail {

DynamicArray<ScoredDocument> select_top_k(std::span<const ScoredDocument> candidates,
                                          std::size_t k) {
    const auto count = std::min(k, candidates.size());
    if (count == 0) {
        return {};
    }

    const WorstScoreFirst worse;
    MinHeap<ScoredDocument, WorstScoreFirst> heap;
    heap.reserve(count);
    for (const auto &candidate : candidates) {
        if (heap.size() < count) {
            heap.push(candidate);
        } else if (worse(heap.top(), candidate)) {
            heap.pop();
            heap.push(candidate);
        }
    }

    DynamicArray<ScoredDocument> results;
    results.reserve(count);
    while (!heap.empty()) {
        results.push_back(heap.top());
        heap.pop();
    }
    std::ranges::reverse(results);
    return results;
}

} // namespace minisearch::detail

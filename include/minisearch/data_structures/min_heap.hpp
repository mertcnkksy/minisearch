#pragma once

#include "minisearch/data_structures/dynamic_array.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace minisearch {

namespace detail {
struct MinHeapTestAccess;
}

// compare(a, b) is true when a belongs closer to the root than b.
// See ADR 0004 for the ordering requirements and reference lifetime.
template <typename T, typename Compare = std::ranges::less>
    requires requires { typename DynamicArray<T>; } && std::is_nothrow_move_constructible_v<T> &&
             std::is_nothrow_move_assignable_v<T> && std::is_object_v<Compare> &&
             std::is_nothrow_copy_constructible_v<Compare> &&
             std::is_nothrow_copy_assignable_v<Compare> &&
             std::is_nothrow_destructible_v<Compare> &&
             requires(const Compare &compare, const T &left, const T &right) {
                 { compare(left, right) } noexcept -> std::same_as<bool>;
             }
class MinHeap {
  public:
    MinHeap() = default;
    explicit MinHeap(const Compare &compare) noexcept : compare_(compare) {}

    // Copies the input and constructs a heap bottom-up in linear time.
    explicit MinHeap(std::span<const T> values, const Compare &compare = Compare{})
        : compare_(compare) {
        data_.reserve(values.size());
        for (const T &value : values) {
            data_.push_back(value);
        }
        for (auto parent = size() / 2; parent != 0; --parent) {
            sift_down(parent - 1);
        }
    }

    ~MinHeap() = default;
    MinHeap(const MinHeap &) = default;

    MinHeap &operator=(const MinHeap &other) {
        if (this != &other) {
            // Copy storage before installing the new ordering. Only the storage copy can throw.
            data_ = other.data_;
            compare_ = other.compare_;
        }
        return *this;
    }

    // Copy the ordering policy so the empty source can still be used after the move.
    MinHeap(MinHeap &&other) noexcept
        : compare_(std::as_const(other.compare_)), data_(std::move(other.data_)) {}

    MinHeap &operator=(MinHeap &&other) noexcept {
        if (this != &other) {
            compare_ = std::as_const(other.compare_);
            data_ = std::move(other.data_);
        }
        return *this;
    }

    // The value may alias top(). See DynamicArray for the element reference precondition.
    void push(const T &value) {
        data_.push_back(value);
        sift_up(size() - 1);
    }

    // Removes the root and retains capacity. An empty heap is unchanged.
    void pop() noexcept {
        if (empty()) {
            return;
        }
        const auto last = size() - 1;
        if (last != 0) {
            data_[0] = std::move(data_[last]);
        }
        data_.pop_back();
        sift_down(0);
    }

    // Throws std::out_of_range on an empty heap.
    [[nodiscard]] const T &top() const {
        if (empty()) {
            throw std::out_of_range("MinHeap is empty");
        }
        return data_[0];
    }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return data_.capacity(); }
    [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

    void reserve(std::size_t count) { data_.reserve(count); }
    void clear() noexcept { data_.clear(); }

  private:
    friend struct detail::MinHeapTestAccess;

    [[nodiscard]] bool precedes(const T &left, const T &right) const noexcept {
        return compare_(left, right);
    }

    void sift_up(std::size_t index) noexcept {
        while (index != 0) {
            const auto parent = (index - 1) / 2;
            if (!precedes(data_[index], data_[parent])) {
                break;
            }
            std::swap(data_[index], data_[parent]);
            index = parent;
        }
    }

    void sift_down(std::size_t index) noexcept {
        const auto count = size();
        // Only indices below count / 2 have a child, so doubling index is safe.
        while (index < count / 2) {
            auto child = index * 2 + 1;
            const auto right = child + 1;
            if (right < count && precedes(data_[right], data_[child])) {
                child = right;
            }
            if (!precedes(data_[child], data_[index])) {
                break;
            }
            std::swap(data_[child], data_[index]);
            index = child;
        }
    }

    [[no_unique_address]] Compare compare_{};
    DynamicArray<T> data_;
};

} // namespace minisearch

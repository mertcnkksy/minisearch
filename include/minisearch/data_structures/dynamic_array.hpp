#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace minisearch {

// Owns contiguous storage for default-constructible, trivially copyable, non-cv elements.
// Only [begin(), end()) is part of the sequence. See ADR 0002 for costs and invalidation.
template <typename T>
    requires(std::is_object_v<T> && std::is_same_v<T, std::remove_cv_t<T>> &&
             std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
class DynamicArray {
  public:
    DynamicArray() noexcept = default;
    ~DynamicArray() = default;

    // Copies the logical elements. Allocation and default-construction errors propagate.
    DynamicArray(const DynamicArray &other)
        : data_(other.copy_storage(other.size_)), size_(other.size_), capacity_(other.size_) {}

    DynamicArray &operator=(const DynamicArray &other) {
        if (this != &other) {
            *this = DynamicArray(other);
        }
        return *this;
    }

    // A moved-from array is empty, with zero capacity.
    DynamicArray(DynamicArray &&other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)),
          capacity_(std::exchange(other.capacity_, 0)) {}

    DynamicArray &operator=(DynamicArray &&other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
            capacity_ = std::exchange(other.capacity_, 0);
        }
        return *this;
    }

    // value must refer to a live T that is not a potentially overlapping subobject.
    // A reference to an existing element is supported, including on growth.
    // Throws length_error above the capacity limit. Allocation and construction errors propagate.
    void push_back(const T &value) {
        if (size_ == capacity_) {
            if (capacity_ == max_capacity) {
                throw std::length_error("DynamicArray capacity limit exceeded");
            }
            const auto doubled = capacity_ > max_capacity / 2 ? max_capacity : capacity_ * 2;
            const auto new_capacity = std::max(std::size_t{1}, doubled);
            auto replacement = copy_storage(new_capacity);
            // Keep the old buffer alive until any aliased element has been copied.
            std::memcpy(static_cast<void *>(replacement.get() + size_), std::addressof(value),
                        sizeof(T));
            data_ = std::move(replacement);
            capacity_ = new_capacity;
        } else {
            std::memmove(static_cast<void *>(data_.get() + size_), std::addressof(value),
                         sizeof(T));
        }
        ++size_;
    }

    // Does nothing on an empty array. Retains storage.
    void pop_back() noexcept {
        if (!empty()) {
            --size_;
        }
    }

    // Does nothing if requested <= capacity(). Throws length_error above the capacity limit.
    // Allocation and default-construction errors propagate without changing the array.
    void reserve(std::size_t requested) {
        if (requested > max_capacity) {
            throw std::length_error("DynamicArray capacity limit exceeded");
        }
        if (requested > capacity_) {
            data_ = copy_storage(requested);
            capacity_ = requested;
        }
    }

    // Removes the logical elements and retains storage.
    void clear() noexcept { size_ = 0; }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    // Precondition: index < size(). Use at() for checked access.
    T &operator[](std::size_t index) noexcept { return data_[index]; }
    const T &operator[](std::size_t index) const noexcept { return data_[index]; }

    // Throws out_of_range if index >= size().
    T &at(std::size_t index) { return const_cast<T &>(std::as_const(*this).at(index)); }

    const T &at(std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("DynamicArray index out of range");
        }
        return data_[index];
    }

    [[nodiscard]] T *begin() noexcept { return data_.get(); }
    [[nodiscard]] const T *begin() const noexcept { return data_.get(); }
    [[nodiscard]] T *end() noexcept { return data_.get() + size_; }
    [[nodiscard]] const T *end() const noexcept { return data_.get() + size_; }

  private:
    static constexpr std::size_t max_capacity =
        std::min(std::numeric_limits<std::size_t>::max() / sizeof(T),
                 static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()));

    [[nodiscard]] std::unique_ptr<T[]> copy_storage(std::size_t capacity) const {
        if (capacity == 0) {
            return {};
        }
        auto replacement = std::make_unique<T[]>(capacity);
        if (size_ != 0) {
            // Copy object representations without requiring copy assignment.
            std::memcpy(static_cast<void *>(replacement.get()), data_.get(), size_ * sizeof(T));
        }
        return replacement;
    }

    std::unique_ptr<T[]> data_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

} // namespace minisearch

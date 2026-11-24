#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace minisearch {

// An open-addressed table with linear probing and a maximum live load of 75%.
// See ADR 0003 for policy requirements, exception guarantees, and pointer invalidation.
template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename KeyEqual = std::equal_to<Key>>
    requires(std::is_object_v<Key> && std::is_object_v<Value> && std::copy_constructible<Key> &&
             std::copy_constructible<Value> &&
             std::predicate<const KeyEqual &, const Key &, const Key &> &&
             requires(const Hash &hash, const Key &key) {
                 { hash(key) } -> std::convertible_to<std::size_t>;
             })
class HashTable {
  public:
    explicit HashTable(Hash hash = Hash{}, KeyEqual equal = KeyEqual{})
        : hash_(std::move(hash)), equal_(std::move(equal)) {}

    HashTable(const HashTable &) = delete;
    HashTable &operator=(const HashTable &) = delete;

    // Returns false for an equivalent key and preserves the stored value.
    bool insert(Key key, Value value) {
        const std::size_t hash = std::as_const(hash_)(key);
        const auto [index, found] = probe(key, hash);
        if (found) {
            return false;
        }

        const auto target = size_ == live_limit(capacity_) ? capacity_for(size_ + 1) : capacity_;
        if (target != capacity_ || (used_ == used_limit(capacity_) && !buckets_[index].deleted)) {
            auto replacement = copy_entries(target);
            vacant_bucket(replacement.get(), target, hash)
                .entry.emplace(hash, std::move(key), std::move(value));
            replace_storage(std::move(replacement), target);
            ++used_;
        } else {
            auto &bucket = buckets_[index];
            bucket.entry.emplace(hash, std::move(key), std::move(value));
            if (!bucket.deleted) {
                ++used_;
            }
            bucket.deleted = false;
        }

        ++size_;
        return true;
    }

    // Returns nullptr when the key is absent. A rehash invalidates returned pointers.
    [[nodiscard]] Value *find(const Key &key) {
        return const_cast<Value *>(std::as_const(*this).find(key));
    }

    [[nodiscard]] const Value *find(const Key &key) const {
        if (size_ == 0) {
            return nullptr;
        }
        const auto [index, found] = probe(key, hash_(key));
        return found ? std::addressof(buckets_[index].entry->value) : nullptr;
    }

    [[nodiscard]] bool contains(const Key &key) const { return find(key) != nullptr; }

    // Returns false for a missing key. Successful erasure invalidates only that value.
    bool erase(const Key &key) {
        if (size_ == 0) {
            return false;
        }
        const auto [index, found] = probe(key, std::as_const(hash_)(key));
        if (!found) {
            return false;
        }

        auto &bucket = buckets_[index];
        bucket.entry.reset();
        bucket.deleted = true;
        --size_;
        return true;
    }

    // Reserves space for count live entries. May throw without changing the table.
    void reserve(std::size_t count) {
        if (count > live_limit(capacity_)) {
            const auto target = capacity_for(count);
            replace_storage(copy_entries(target), target);
        }
    }

    // Removes all entries and tombstones while retaining capacity.
    void clear() noexcept {
        for (std::size_t index = 0; index < capacity_; ++index) {
            buckets_[index].entry.reset();
            buckets_[index].deleted = false;
        }
        size_ = 0;
        used_ = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] double load_factor() const noexcept {
        return capacity_ == 0 ? 0.0 : static_cast<double>(size_) / static_cast<double>(capacity_);
    }
    [[nodiscard]] static constexpr double max_load_factor() noexcept { return 0.75; }

  private:
    struct Entry {
        std::size_t hash;
        Key key;
        Value value;
    };

    struct Bucket {
        std::optional<Entry> entry;
        bool deleted = false;
    };

    struct ProbeResult {
        std::size_t index;
        bool found;
    };

    static constexpr std::size_t minimum_capacity = 8;
    static constexpr std::size_t maximum_capacity = std::bit_floor(
        static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(Bucket));

    [[nodiscard]] static constexpr std::size_t live_limit(std::size_t capacity) noexcept {
        return capacity - capacity / 4;
    }

    [[nodiscard]] static constexpr std::size_t used_limit(std::size_t capacity) noexcept {
        return capacity - capacity / 8;
    }

    [[nodiscard]] std::size_t capacity_for(std::size_t count) const {
        if (maximum_capacity < minimum_capacity || count > live_limit(maximum_capacity)) {
            throw std::length_error("HashTable capacity limit exceeded");
        }
        auto capacity = capacity_ == 0 ? minimum_capacity : capacity_;
        while (live_limit(capacity) < count) {
            capacity *= 2;
        }
        return capacity;
    }

    [[nodiscard]] ProbeResult probe(const Key &key, std::size_t hash) const {
        if (capacity_ == 0) {
            return {0, false};
        }
        const auto mask = capacity_ - 1;
        auto index = hash & mask;
        auto first_deleted = capacity_;
        // The used limit guarantees that the probe reaches a never-used slot on a miss.
        for (;;) {
            const auto &bucket = buckets_[index];
            if (bucket.entry) {
                if (bucket.entry->hash == hash && equal_(bucket.entry->key, key)) {
                    return {index, true};
                }
            } else if (!bucket.deleted) {
                return {first_deleted == capacity_ ? index : first_deleted, false};
            } else if (first_deleted == capacity_) {
                first_deleted = index;
            }
            index = (index + 1) & mask;
        }
    }

    // The fresh table contains no tombstones or equivalent keys.
    [[nodiscard]] static Bucket &vacant_bucket(Bucket *buckets, std::size_t capacity,
                                               std::size_t hash) noexcept {
        const auto mask = capacity - 1;
        auto index = hash & mask;
        while (buckets[index].entry) {
            index = (index + 1) & mask;
        }
        return buckets[index];
    }

    [[nodiscard]] std::unique_ptr<Bucket[]> copy_entries(std::size_t capacity) const {
        auto replacement = std::make_unique<Bucket[]>(capacity);
        for (std::size_t index = 0; index < capacity_; ++index) {
            const auto &entry = buckets_[index].entry;
            if (entry) {
                vacant_bucket(replacement.get(), capacity, entry->hash).entry.emplace(*entry);
            }
        }
        return replacement;
    }

    void replace_storage(std::unique_ptr<Bucket[]> replacement, std::size_t capacity) noexcept {
        buckets_ = std::move(replacement);
        capacity_ = capacity;
        used_ = size_;
    }

    [[no_unique_address]] Hash hash_;
    [[no_unique_address]] KeyEqual equal_;
    std::unique_ptr<Bucket[]> buckets_;
    std::size_t capacity_ = 0;
    std::size_t size_ = 0;
    std::size_t used_ = 0; // Live entries and tombstones.
};

} // namespace minisearch

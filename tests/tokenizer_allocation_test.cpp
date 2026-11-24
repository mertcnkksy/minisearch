#include "minisearch/text/tokenizer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {

thread_local bool recording = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t fail_at = 0;

class ScopedAllocationProbe {
  public:
    explicit ScopedAllocationProbe(
        std::size_t failure_index = std::numeric_limits<std::size_t>::max()) noexcept {
        allocations = 0;
        fail_at = failure_index;
        recording = true;
    }

    ~ScopedAllocationProbe() { recording = false; }

    ScopedAllocationProbe(const ScopedAllocationProbe &) = delete;
    ScopedAllocationProbe &operator=(const ScopedAllocationProbe &) = delete;

    [[nodiscard]] std::size_t count() const noexcept { return allocations; }
};

} // namespace

// Only the isolated tokenizer allocation-test process uses these replacements.
void *operator new(std::size_t bytes) {
    if (recording && allocations++ == fail_at) {
        throw std::bad_alloc{};
    }
    if (void *memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        return memory;
    }
    throw std::bad_alloc{};
}

// Route nothrow allocations through the same allocator as ordinary scalar deletes.
void *operator new(std::size_t bytes, const std::nothrow_t &) noexcept {
    try {
        return ::operator new(bytes);
    } catch (const std::bad_alloc &) {
        return nullptr;
    }
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete(void *memory, const std::nothrow_t &) noexcept { ::operator delete(memory); }

namespace {

TEST(TokenizerAllocationTest, EveryInjectedAllocationFailureRollsBackAndAllowsRetry) {
    using minisearch::Token;
    using minisearch::Tokenizer;

    const auto saved = Tokenizer::tokenize("Previous result");
    const std::vector<Token> saved_expected{{"previous", 0, 0, 8}, {"result", 1, 9, 15}};
    std::string input(8192, 'A');
    for (int index = 0; index < 128; ++index) {
        input += " x9";
    }
    input += ' ';
    input.append(8192, 'B');

    std::vector<Token> expected;
    std::size_t count = 0;
    {
        const ScopedAllocationProbe probe;
        expected = Tokenizer::tokenize(input);
        count = probe.count();
    }
    ASSERT_EQ(expected.size(), 130U);
    ASSERT_GT(count, 1U);

    // Calibrate against the successful call instead of assuming a growth factor or SSO size.
    for (std::size_t index = 0; index < count; ++index) {
        SCOPED_TRACE(index);
        EXPECT_THROW(
            {
                const ScopedAllocationProbe probe(index);
                static_cast<void>(Tokenizer::tokenize(input));
            },
            std::bad_alloc);
        EXPECT_EQ(saved, saved_expected);
        EXPECT_EQ(Tokenizer::tokenize(input), expected);
    }
}

} // namespace

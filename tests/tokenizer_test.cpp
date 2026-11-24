#include "minisearch/text/tokenizer.hpp"

#include "minisearch/data_structures/dynamic_array.hpp"
#include "minisearch/data_structures/hash_table.hpp"
#include "text/token_position.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <locale>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using minisearch::Token;
using minisearch::Tokenizer;

constexpr char reference_lowercase_or_separator(char byte) {
    constexpr std::string_view upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr std::string_view lower = "abcdefghijklmnopqrstuvwxyz";
    constexpr std::string_view digits = "0123456789";
    const auto index = upper.find(byte);
    if (index != std::string_view::npos) {
        return lower[index];
    }
    return lower.find(byte) != std::string_view::npos || digits.find(byte) != std::string_view::npos
               ? byte
               : ' ';
}

// The reference masks separators first, then finds runs in the normalized copy.
std::vector<Token> reference_tokens(std::string_view input) {
    std::string masked(input);
    std::ranges::transform(input, masked.begin(), reference_lowercase_or_separator);
    std::vector<Token> result;
    auto start = masked.find_first_not_of(' ');
    while (start != std::string::npos) {
        const auto end = std::min(masked.find(' ', start), masked.size());
        result.push_back({masked.substr(start, end - start),
                          static_cast<std::uint32_t>(result.size()), start, end});
        start = masked.find_first_not_of(' ', end);
    }
    return result;
}

std::string as_hex(std::string_view input) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string encoded;
    for (const char value : input) {
        const auto byte = static_cast<unsigned char>(value);
        encoded += digits[byte >> 4];
        encoded += digits[byte & 0x0F];
    }
    return encoded;
}

void expect_rejection(std::string_view input, std::size_t offset) {
    try {
        static_cast<void>(Tokenizer::tokenize(input));
        ADD_FAILURE() << "Expected invalid_argument at byte offset " << offset;
    } catch (const std::invalid_argument &error) {
        EXPECT_EQ(std::string_view(error.what()),
                  "Non-ASCII byte at offset " + std::to_string(offset));
    }
}

void expect_invariants(std::string_view input, const std::vector<Token> &tokens) {
    std::size_t previous_end = 0;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const auto &token = tokens[index];
        EXPECT_EQ(token.position, index);
        ASSERT_LE(previous_end, token.start_offset);
        ASSERT_LT(token.start_offset, token.end_offset);
        ASSERT_LE(token.end_offset, input.size());

        const auto source = input.substr(token.start_offset, token.end_offset - token.start_offset);
        EXPECT_TRUE(std::ranges::equal(token.term, source,
                                       [](char normalized, char original) {
                                           return normalized != ' ' &&
                                                  normalized ==
                                                      reference_lowercase_or_separator(original);
                                       }))
            << "token index " << index;
        previous_end = token.end_offset;
    }
}

TEST(TokenizerTest, EmptyAndSeparatorOnlyInputsProduceNoTokens) {
    EXPECT_TRUE(Tokenizer::tokenize(std::string_view{}).empty());
    EXPECT_TRUE(Tokenizer::tokenize("").empty());
    EXPECT_TRUE(Tokenizer::tokenize(" \t\r\n\v\f").empty());
    EXPECT_TRUE(Tokenizer::tokenize(".,!?----__'").empty());
    constexpr std::array controls{'\0', '\x01', '\x1F', '\x7F'};
    EXPECT_TRUE(Tokenizer::tokenize({controls.data(), controls.size()}).empty());
}

TEST(TokenizerTest, SplitsAsciiWordsAndTechnicalText) {
    struct Example {
        std::string_view input;
        std::vector<std::string> terms;
    };
    const std::array examples{
        Example{"Z", {"z"}},
        Example{"word", {"word"}},
        Example{"M iXeD", {"m", "ixed"}},
        Example{"Hello WORLD", {"hello", "world"}},
        Example{"data-structures", {"data", "structures"}},
        Example{"hello_world", {"hello", "world"}},
        Example{"C++20 C#", {"c", "20", "c"}},
        Example{"version2 v1.0.0", {"version2", "v1", "0", "0"}},
        Example{"don't", {"don", "t"}},
        Example{"123 ABC456", {"123", "abc456"}},
        Example{"one...two!!!", {"one", "two"}},
        Example{"Python Python", {"python", "python"}},
        Example{"foo@example.com 3.14159", {"foo", "example", "com", "3", "14159"}},
    };
    for (const auto &[input, terms] : examples) {
        SCOPED_TRACE(input);
        const auto tokens = Tokenizer::tokenize(input);
        ASSERT_EQ(tokens.size(), terms.size());
        for (std::size_t index = 0; index < terms.size(); ++index) {
            EXPECT_EQ(tokens[index].term, terms[index]);
        }
        expect_invariants(input, tokens);
    }
}

TEST(TokenizerTest, RecordsHalfOpenOffsetsAcrossPunctuationAndSpaces) {
    EXPECT_EQ(Tokenizer::tokenize("Hello, World!"),
              (std::vector<Token>{{"hello", 0, 0, 5}, {"world", 1, 7, 12}}));
    constexpr std::string_view text = " \tHi,  HI\r\nHi\nZ9  ";
    const auto tokens = Tokenizer::tokenize(text);
    EXPECT_EQ(tokens, (std::vector<Token>{
                          {"hi", 0, 2, 4}, {"hi", 1, 7, 9}, {"hi", 2, 11, 13}, {"z9", 3, 14, 16}}));
    expect_invariants(text, tokens);
}

TEST(TokenizerTest, EmbeddedNullAndControlsPreserveByteCoordinates) {
    constexpr char bytes[] = "X\0A9\x01"
                             "Z\x7F"
                             "Last";
    const std::string_view text{bytes, sizeof(bytes) - 1};
    const auto tokens = Tokenizer::tokenize(text);
    EXPECT_EQ(tokens, (std::vector<Token>{
                          {"x", 0, 0, 1}, {"a9", 1, 2, 4}, {"z", 2, 5, 6}, {"last", 3, 7, 11}}));
    expect_invariants(text, tokens);
}

TEST(TokenizerTest, ClassifiesAll256ByteValues) {
    for (unsigned value = 0; value < 256; ++value) {
        SCOPED_TRACE(value);
        const auto byte = static_cast<char>(value);
        const std::array text{'A', byte, 'Z'};
        const std::string_view input{text.data(), text.size()};
        if (value >= 128) {
            expect_rejection(input, 1);
            continue;
        }
        const auto normalized = reference_lowercase_or_separator(byte);
        if (normalized == ' ') {
            EXPECT_EQ(Tokenizer::tokenize(input),
                      (std::vector<Token>{{"a", 0, 0, 1}, {"z", 1, 2, 3}}));
        } else {
            EXPECT_EQ(Tokenizer::tokenize(input),
                      (std::vector<Token>{{std::string{'a', normalized, 'z'}, 0, 0, 3}}));
        }
    }
}

TEST(TokenizerTest, RejectsNonAsciiAtTheBeginningMiddleAndEnd) {
    expect_rejection("\xEF\xBB\xBF"
                     "ASCII",
                     0);
    expect_rejection("\x80", 0);
    expect_rejection("\xE2(\xA1", 0);
    expect_rejection("Istanbul \xC4\xB0"
                     "zmir",
                     9);
    expect_rejection("emoji \xF0\x9F\x99\x82 ok", 6);
    expect_rejection("before\xFF"
                     " after \x80",
                     6);
    expect_rejection("token\x80", 5);

    constexpr char text[] = "A\0B\xFF"
                            "C";
    expect_rejection({text, sizeof(text) - 1}, 3);
    EXPECT_EQ(Tokenizer::tokenize("Retry"), (std::vector<Token>{{"retry", 0, 0, 5}}));
}

TEST(TokenizerTest, PositionGuardAcceptsTheLastValueAndRejectsTheFirstOverflow) {
    constexpr std::uintmax_t last = std::numeric_limits<std::uint32_t>::max();
    static_assert(minisearch::detail::checked_token_position(last) == last);
    EXPECT_EQ(minisearch::detail::checked_token_position(last - 1), last - 1);
    EXPECT_THROW(static_cast<void>(minisearch::detail::checked_token_position(last + 1)),
                 std::length_error);
}

TEST(TokenizerTest, TermsOutliveTemporaryDestroyedAndModifiedInput) {
    const auto from_temporary = Tokenizer::tokenize(std::string(256, 'Q'));
    ASSERT_EQ(from_temporary.size(), 1U);
    EXPECT_EQ(from_temporary.front().term, std::string(256, 'q'));

    std::vector<Token> saved;
    {
        std::string input = std::string(128, 'A') + " RePeAt REPEAT";
        const auto original = input;
        saved = Tokenizer::tokenize(input);
        EXPECT_EQ(input, original);
        input.assign(input.size(), '!');
    }

    ASSERT_EQ(saved.size(), 3U);
    EXPECT_EQ(saved[0], (Token{std::string(128, 'a'), 0, 0, 128}));
    EXPECT_EQ(saved[1], (Token{"repeat", 1, 129, 135}));
    EXPECT_EQ(saved[2], (Token{"repeat", 2, 136, 142}));

    auto other = Tokenizer::tokenize("RePeAt");
    ASSERT_EQ(other.size(), 1U);
    other.front().term = "changed";
    EXPECT_EQ(saved[1].term, "repeat");
}

TEST(TokenizerTest, RepeatedCallsAreDeterministicAndNormalizedTermsAreIdempotent) {
    const std::string input = "I Mixed-CaSe lower42 123 I";
    const auto first = Tokenizer::tokenize(input);
    EXPECT_EQ(Tokenizer::tokenize(input), first);
    EXPECT_EQ(input, "I Mixed-CaSe lower42 123 I");
    for (const auto &token : first) {
        EXPECT_EQ(Tokenizer::tokenize(token.term),
                  (std::vector<Token>{{token.term, 0, 0, token.term.size()}}));
    }
}

class AlternateCtype : public std::ctype<char> {
  protected:
    char do_tolower(char value) const override {
        return value == 'I' ? '!' : std::ctype<char>::do_tolower(value);
    }
};

class ScopedGlobalLocale {
  public:
    explicit ScopedGlobalLocale(const std::locale &locale)
        : previous_(std::locale::global(locale)) {}
    ~ScopedGlobalLocale() { std::locale::global(previous_); }

    ScopedGlobalLocale(const ScopedGlobalLocale &) = delete;
    ScopedGlobalLocale &operator=(const ScopedGlobalLocale &) = delete;

  private:
    std::locale previous_;
};

TEST(TokenizerTest, NormalizationIgnoresTheGlobalCppLocale) {
    const std::locale altered(std::locale::classic(), new AlternateCtype);
    const ScopedGlobalLocale global(altered);
    ASSERT_EQ(std::tolower('I', std::locale()), '!');
    EXPECT_EQ(Tokenizer::tokenize("I I9"), (std::vector<Token>{{"i", 0, 0, 1}, {"i9", 1, 2, 4}}));
}

TEST(TokenizerTest, HandlesLongTermsAndLargeSeparatorRuns) {
    constexpr std::size_t length = 1U << 20;
    const std::string input(length, 'Z');
    const auto tokens = Tokenizer::tokenize(input);
    ASSERT_EQ(tokens.size(), 1U);
    EXPECT_EQ(tokens.front(), (Token{std::string(length, 'z'), 0, 0, length}));
    EXPECT_TRUE(Tokenizer::tokenize(std::string(length, '!')).empty());
}

TEST(TokenizerTest, ScansAnOverOneMiBDocumentAndRejectsAnInvalidTail) {
    constexpr std::string_view block = "--Go7__A9...ONE\t\r\n";
    constexpr std::size_t repeats = (1U << 20) / block.size() + 1;
    std::string text;
    text.reserve(repeats * block.size() + 4);
    for (std::size_t index = 0; index < repeats; ++index) {
        text += block;
    }
    text += "End9";
    ASSERT_GE(text.size(), 1U << 20);

    {
        const auto tokens = Tokenizer::tokenize(text);
        ASSERT_EQ(tokens.size(), repeats * 3 + 1);
        EXPECT_EQ(tokens.front(), (Token{"go7", 0, 2, 5}));
        EXPECT_EQ(tokens[1], (Token{"a9", 1, 7, 9}));
        EXPECT_EQ(tokens[2], (Token{"one", 2, 12, 15}));
        EXPECT_EQ(tokens.back(), (Token{"end9", static_cast<std::uint32_t>(repeats * 3),
                                        text.size() - 4, text.size()}));
        expect_invariants(text, tokens);
    }

    text += static_cast<char>(0x80);
    expect_rejection(text, text.size() - 1);
}

TEST(TokenizerTest, MatchesAnIndependentReferenceAcrossTenThousandAsciiInputs) {
    constexpr std::uint32_t seed = 0x604A5C11U;
    std::mt19937 random(seed);
    for (int sample = 0; sample < 10'000; ++sample) {
        std::string input(static_cast<std::size_t>(random() % 161U), '\0');
        for (char &byte : input) {
            byte = static_cast<char>(random() % 128U);
        }
        SCOPED_TRACE(::testing::Message()
                     << "seed=" << seed << " sample=" << sample << " bytes=" << as_hex(input));
        const auto tokens = Tokenizer::tokenize(input);
        ASSERT_EQ(tokens, reference_tokens(input));
        expect_invariants(input, tokens);
    }
}

TEST(TokenizerTest, ReportsFirstHighByteInSeededMixedInputs) {
    constexpr std::uint32_t seed = 0x9BAD80U;
    std::mt19937 random(seed);
    for (int sample = 0; sample < 512; ++sample) {
        std::string input(static_cast<std::size_t>(random() % 96U), '\0');
        for (char &byte : input) {
            byte = static_cast<char>(random() % 128U);
        }
        const auto first_invalid = input.size();
        input += static_cast<char>(128U + random() % 128U);
        input += " trailing ";
        input += static_cast<char>(128U + random() % 128U);
        SCOPED_TRACE(::testing::Message()
                     << "seed=" << seed << " sample=" << sample << " bytes=" << as_hex(input));
        expect_rejection(input, first_invalid);
    }
}

TEST(TokenizerTest, SuppliesDocumentLengthFrequenciesAndPositionsForFutureIndexing) {
    using Positions = minisearch::DynamicArray<std::uint32_t>;
    minisearch::HashTable<std::string, Positions> postings;

    const auto tokens = Tokenizer::tokenize("Python supports data structures. Python is powerful.");
    const std::vector<Token> expected{{"python", 0, 0, 6},    {"supports", 1, 7, 15},
                                      {"data", 2, 16, 20},    {"structures", 3, 21, 31},
                                      {"python", 4, 33, 39},  {"is", 5, 40, 42},
                                      {"powerful", 6, 43, 51}};
    ASSERT_EQ(tokens, expected);
    EXPECT_EQ(tokens.size(), 7U);

    for (const auto &token : tokens) {
        auto *positions = postings.find(token.term);
        if (positions == nullptr) {
            ASSERT_TRUE(postings.insert(token.term, Positions{}));
            positions = postings.find(token.term);
        }
        ASSERT_NE(positions, nullptr);
        positions->push_back(token.position);
    }
    EXPECT_EQ(postings.size(), 6U);
    const auto *python = postings.find("python");
    ASSERT_NE(python, nullptr);
    EXPECT_TRUE(std::ranges::equal(*python, std::array<std::uint32_t, 2>{0, 4}));

    for (const auto &token : tokens) {
        if (token.term != "python") {
            const auto *positions = postings.find(token.term);
            ASSERT_NE(positions, nullptr);
            EXPECT_TRUE(std::ranges::equal(*positions, std::array{token.position}));
        }
    }

    const auto query = Tokenizer::tokenize("DATA Structures PYTHON");
    ASSERT_EQ(query.size(), 3U);
    EXPECT_EQ(query[0].term, tokens[2].term);
    EXPECT_EQ(query[1].term, tokens[3].term);
    EXPECT_EQ(query[2].term, tokens[4].term);
    EXPECT_EQ(tokens[2].position + 1, tokens[3].position);
}

} // namespace

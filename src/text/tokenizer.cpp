#include "minisearch/text/tokenizer.hpp"

#include "token_position.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace minisearch {

namespace {

constexpr bool is_ascii_upper(unsigned char byte) noexcept { return byte >= 0x41 && byte <= 0x5A; }

constexpr bool is_token_byte(unsigned char byte) noexcept {
    return is_ascii_upper(byte) || (byte >= 0x61 && byte <= 0x7A) || (byte >= 0x30 && byte <= 0x39);
}

} // namespace

std::vector<Token> Tokenizer::tokenize(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t cursor = 0;

    while (cursor < text.size()) {
        const auto byte = static_cast<unsigned char>(text[cursor]);
        if (byte > 0x7F) {
            throw std::invalid_argument("Non-ASCII byte at offset " + std::to_string(cursor));
        }
        if (!is_token_byte(byte)) {
            ++cursor;
            continue;
        }

        const auto start = cursor++;
        while (cursor < text.size() && is_token_byte(static_cast<unsigned char>(text[cursor]))) {
            ++cursor;
        }

        const auto position = detail::checked_token_position(tokens.size());
        std::string term(text.substr(start, cursor - start));
        for (char &letter : term) {
            const auto code = static_cast<unsigned char>(letter);
            if (is_ascii_upper(code)) {
                letter = static_cast<char>(code + 0x20);
            }
        }
        tokens.push_back({std::move(term), position, start, cursor});
    }

    return tokens;
}

} // namespace minisearch

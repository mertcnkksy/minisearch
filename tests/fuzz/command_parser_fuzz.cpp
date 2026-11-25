#include "check.hpp"
#include "cli/command_parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

using minisearch::cli::Command;
using minisearch::cli::ParsedCommand;

std::string_view trim_left(std::string_view text) {
    while (!text.empty() && (text[0] == ' ' || text[0] == '\t')) {
        text.remove_prefix(1);
    }
    return text;
}

std::string_view head(std::string_view text) {
    const auto end = std::ranges::find_if(text, [](char c) { return c == ' ' || c == '\t'; });
    return text.substr(0, static_cast<std::size_t>(end - text.begin()));
}

std::optional<std::size_t> decimal(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::size_t number = 0;
    for (const auto c : value) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::size_t>(c - '0');
        if (number > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        number = number * 10 + digit;
    }
    return number;
}

std::optional<ParsedCommand> expected_parse(std::string_view input) {
    if (input.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    const auto text = trim_left(input);
    const auto word = head(text);
    auto payload = trim_left(text.substr(word.size()));
    if (word.empty()) {
        return ParsedCommand{Command::blank};
    }
    if (word == "add" || word == "index") {
        if (payload.empty()) {
            return std::nullopt;
        }
        if (word == "index") {
            while (payload.back() == ' ' || payload.back() == '\t') {
                payload.remove_suffix(1);
            }
        }
        return ParsedCommand{word == "index" ? Command::index : Command::add, payload};
    }
    if (word == "search") {
        auto limit = minisearch::cli::default_result_limit;
        if (head(payload) == "--top") {
            payload = trim_left(payload.substr(5));
            const auto digits = head(payload);
            const auto parsed = decimal(digits);
            if (!parsed) {
                return std::nullopt;
            }
            limit = *parsed;
            payload = trim_left(payload.substr(digits.size()));
        }
        const auto option = head(payload);
        if (option == "--") {
            payload = trim_left(payload.substr(2));
        } else if (option.starts_with('-') && std::ranges::any_of(option, [](char c) {
                       return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z');
                   })) {
            return std::nullopt;
        }
        if (!payload.empty()) {
            return ParsedCommand{Command::search, payload, limit};
        }
    }
    if (payload.empty()) {
        if (word == "help")
            return ParsedCommand{Command::help};
        if (word == "stats")
            return ParsedCommand{Command::stats};
        if (word == "exit" || word == "quit")
            return ParsedCommand{Command::exit};
    }
    return std::nullopt;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    if (size > 4096) {
        return 0;
    }
    const std::string_view input(reinterpret_cast<const char *>(bytes), size);
    const auto expected = expected_parse(input);
    using minisearch::test::must;
    try {
        const auto actual = minisearch::cli::parse_command(input);
        must(expected.has_value(), "Accepted invalid command");
        must(actual.command == expected->command && actual.limit == expected->limit &&
                 actual.payload.size() == expected->payload.size(),
             "Wrong command, count, or payload length");
        must(actual.payload.empty() || actual.payload.data() == expected->payload.data(),
             "Wrong borrowed payload position");
    } catch (const std::invalid_argument &) {
        must(!expected, "Rejected valid command");
    }
    return 0;
}

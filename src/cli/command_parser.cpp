#include "command_parser.hpp"

#include <charconv>
#include <stdexcept>
#include <string>
#include <system_error>

namespace minisearch::cli {

namespace {

constexpr std::string_view separators = " \t";
constexpr std::string_view option_name_bytes =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

std::string_view take_word(std::string_view &text) {
    const auto first = text.find_first_not_of(separators);
    if (first == std::string_view::npos) {
        text = {};
        return {};
    }
    text.remove_prefix(first);
    const auto word = text.substr(0, text.find_first_of(separators));
    const auto next = text.find_first_not_of(separators, word.size());
    text = next == std::string_view::npos ? std::string_view{} : text.substr(next);
    return word;
}

std::size_t parse_limit(std::string_view token) {
    if (!token.empty()) {
        std::size_t limit = 0;
        const auto *end = token.data() + token.size();
        const auto [next, error] = std::from_chars(token.data(), end, limit, 10);
        if (error == std::errc{} && next == end) {
            return limit;
        }
    }
    throw std::invalid_argument("Invalid result limit. Expected an unsigned decimal integer.");
}

ParsedCommand parse_search(std::string_view query) {
    std::size_t limit = default_result_limit;
    auto remaining = query;
    auto first = take_word(remaining);
    if (first == "--top") {
        limit = parse_limit(take_word(remaining));
        query = remaining;
        first = take_word(remaining);
    }

    if (first == "--") {
        query = remaining;
    } else if (first.starts_with('-') &&
               first.find_first_of(option_name_bytes, 1) != std::string_view::npos) {
        throw std::invalid_argument("Unknown search option: " + std::string(first));
    }
    if (query.empty()) {
        throw std::invalid_argument("Usage: search [--top K] [--] <query>");
    }
    return {Command::search, query, limit};
}

ParsedCommand no_arguments(Command command, std::string_view name, std::string_view payload) {
    if (!payload.empty()) {
        throw std::invalid_argument("Unexpected arguments for " + std::string(name) + '.');
    }
    return {command};
}

} // namespace

ParsedCommand parse_command(std::string_view line) {
    if (line.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Command lines cannot contain NUL bytes.");
    }
    const auto name = take_word(line);
    if (name.empty()) {
        return {Command::blank};
    }
    if (name == "help") {
        return no_arguments(Command::help, name, line);
    }
    if (name == "stats") {
        return no_arguments(Command::stats, name, line);
    }
    if (name == "exit" || name == "quit") {
        return no_arguments(Command::exit, name, line);
    }
    if (name == "add") {
        if (line.empty()) {
            throw std::invalid_argument("Usage: add <text>");
        }
        return {Command::add, line};
    }
    if (name == "index") {
        if (line.empty()) {
            throw std::invalid_argument("Usage: index <path>");
        }
        return {Command::index, line.substr(0, line.find_last_not_of(separators) + 1)};
    }
    if (name == "search") {
        return parse_search(line);
    }
    throw std::invalid_argument("Unknown command: " + std::string(name));
}

} // namespace minisearch::cli

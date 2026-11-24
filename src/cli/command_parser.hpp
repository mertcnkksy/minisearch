#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace minisearch::cli {

inline constexpr std::size_t max_command_bytes = 1'048'576;
inline constexpr std::size_t default_result_limit = 10;

enum class Command : std::uint8_t { blank, help, add, index, search, stats, exit };

struct ParsedCommand {
    Command command;
    std::string_view payload{};
    std::size_t limit = default_result_limit;
};

// The payload borrows the command line. The caller handles line endings.
[[nodiscard]] ParsedCommand parse_command(std::string_view line);

} // namespace minisearch::cli

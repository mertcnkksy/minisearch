#include "minisearch/cli/repl.hpp"

#include "command_parser.hpp"
#include "file_reader.hpp"
#include "minisearch/index/inverted_index.hpp"
#include "minisearch/search/search_engine.hpp"
#include "terminal_output.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace minisearch::cli {

namespace {

enum class LineKind : std::uint8_t { ready, end, oversized };

struct CommandLine {
    LineKind kind;
    std::string_view text{};
};

// getline counts the LF in gcount() but excludes it from the buffer. EOF and
// the delimiter are checked before the size bound, so an exact fit is valid.
CommandLine read_line(std::istream &input, char *buffer) {
    try {
        input.getline(buffer, static_cast<std::streamsize>(max_command_bytes + 1), '\n');
    } catch (const std::ios_base::failure &) {
        if (!input.fail() && !input.eof()) {
            throw;
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Input stream failure.");
    }
    const auto count = static_cast<std::size_t>(input.gcount());
    if (input.eof()) {
        return count == 0 ? CommandLine{LineKind::end}
                          : CommandLine{LineKind::ready, {buffer, count}};
    }
    if (input.fail()) {
        if (count != max_command_bytes) {
            throw std::runtime_error("Input stream failure.");
        }
        input.clear();
        try {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } catch (const std::ios_base::failure &) {
            if (!input.eof() || input.bad()) {
                throw;
            }
        }
        if (input.bad() || (input.fail() && !input.eof())) {
            throw std::runtime_error("Input stream failure.");
        }
        return {LineKind::oversized};
    }

    std::string_view line(buffer, count - 1);
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return {LineKind::ready, line};
}

void flush_output(std::ostream &output) {
    if (!output.flush()) {
        throw std::runtime_error("Output stream failure.");
    }
}

void diagnostic(std::ostream &errors, std::string_view prefix, std::string_view message) {
    write_text(errors, prefix);
    write_escaped(errors, message);
    errors.put('\n');
    if (!errors.flush()) {
        throw std::runtime_error("Error stream failure.");
    }
}

void fatal(std::ostream &errors, std::string_view message) noexcept {
    try {
        diagnostic(errors, "Fatal: ", message);
    } catch (...) {
        // A failure while reporting a fatal error cannot prevent exit code 1.
    }
}

template <typename Number> void write_number(std::ostream &output, Number value) {
    std::array<char, 384> text{};
    std::to_chars_result result;
    if constexpr (std::is_floating_point_v<Number>) {
        result = std::to_chars(text.data(), text.data() + text.size(), value,
                               std::chars_format::fixed, 6);
    } else {
        result = std::to_chars(text.data(), text.data() + text.size(), value);
    }
    if (result.ec != std::errc{}) {
        throw std::runtime_error("Cannot format number.");
    }
    write_text(output, {text.data(), static_cast<std::size_t>(result.ptr - text.data())});
}

template <typename Number>
void write_stat(std::ostream &output, std::string_view label, Number value) {
    write_text(output, label);
    write_number(output, value);
    output.put('\n');
}

void print_results(std::ostream &output, const DynamicArray<ScoredDocument> &results) {
    if (results.empty()) {
        write_text(output, "No results.\n");
        return;
    }

    write_text(output, "Rank  Doc ID  Score\n");
    std::size_t rank = 0;
    for (const auto &result : results) {
        write_number(output, ++rank);
        write_text(output, "     ");
        write_number(output, result.document_id);
        write_text(output, "       ");
        write_number(output, result.score);
        output.put('\n');
    }
    write_text(output, "\nReturned ");
    write_number(output, results.size());
    write_text(output, results.size() == 1 ? " result.\n" : " results.\n");
}

void print_indexed(std::ostream &output, DocumentId document) {
    write_text(output, "Indexed document ");
    write_number(output, document);
    write_text(output, ".\n");
}

bool dispatch(ParsedCommand command, InvertedIndex &index, std::ostream &output) {
    switch (command.command) {
    case Command::blank:
        break;
    case Command::help:
        write_text(output, "Available commands\n\n"
                           "  add <text>\n"
                           "  index <path>\n"
                           "  search [--top K] [--] <query>\n"
                           "  stats\n"
                           "  help\n"
                           "  exit\n"
                           "  quit\n\n"
                           "Documents are stored in memory for the current session.\n"
                           "Queries and document contents must be ASCII.\n"
                           "Default result limit: 10. File size limit: 16 MiB.\n");
        break;
    case Command::add:
        print_indexed(output, index.add_document(command.payload));
        break;
    case Command::index: {
        const auto contents = read_document_file(command.payload);
        print_indexed(output, index.add_document(contents));
        break;
    }
    case Command::search:
        print_results(output, search(index, command.payload, command.limit));
        break;
    case Command::stats:
        write_stat(output, "Documents:      ", index.document_count());
        write_stat(output, "Unique terms:   ", index.unique_term_count());
        write_stat(output, "Total tokens:   ", index.total_token_count());
        write_stat(output, "Avg. length:    ", index.average_document_length());
        break;
    case Command::exit:
        return false;
    }
    return true;
}

bool run_command(std::string_view line, InvertedIndex &index, std::ostream &output,
                 std::ostream &errors) {
    const auto recover = [&](const std::exception &error) {
        if (!output) {
            throw std::runtime_error("Output stream failure.");
        }
        diagnostic(errors, "Error: ", error.what());
    };
    try {
        const bool keep_running = dispatch(parse_command(line), index, output);
        if (!output) {
            throw std::runtime_error("Output stream failure.");
        }
        return keep_running;
    } catch (const std::invalid_argument &error) {
        recover(error);
    } catch (const std::length_error &error) {
        recover(error);
    } catch (const std::overflow_error &error) {
        recover(error);
    } catch (const FileError &error) {
        recover(error);
    } catch (const std::filesystem::filesystem_error &error) {
        recover(error);
    }
    return true;
}

void run_session(std::istream &input, std::ostream &output, std::ostream &errors) {
    InvertedIndex index;
    const auto buffer = std::make_unique_for_overwrite<char[]>(max_command_bytes + 1);
    write_text(output, "MiniSearch\n"
                       "In-memory document search engine.\n"
                       "Type 'help' for available commands.\n\n");
    for (;;) {
        write_text(output, "minisearch> ");
        flush_output(output);
        const auto line = read_line(input, buffer.get());
        if (line.kind == LineKind::end) {
            break;
        }
        if (line.kind == LineKind::oversized) {
            diagnostic(errors, "Error: ", "Command exceeds the 1 MiB byte limit.");
        } else if (!run_command(line.text, index, output, errors)) {
            break;
        }
        if (input.eof()) {
            break;
        }
    }
    flush_output(output);
}

} // namespace

int run_repl(std::istream &input, std::ostream &output, std::ostream &errors) {
    try {
        run_session(input, output, errors);
        return 0;
    } catch (const std::bad_alloc &) {
        fatal(errors, "Out of memory.");
    } catch (const std::exception &error) {
        fatal(errors, error.what());
    } catch (...) {
        fatal(errors, "Unexpected runtime failure.");
    }
    return 1;
}

} // namespace minisearch::cli

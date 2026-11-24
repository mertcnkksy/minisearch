#include "file_reader.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace minisearch::cli {

namespace {

constexpr auto size_limit_error = "File exceeds the 16 MiB byte limit.";
constexpr auto size_changed_error = "File size changed while reading.";

} // namespace

std::string read_document_bytes(std::istream &input, std::size_t expected_size) {
    if (expected_size > max_file_bytes) {
        throw FileError(size_limit_error);
    }

    std::string contents;
    contents.reserve(expected_size);
    std::array<char, 8192> chunk{};
    do {
        // Read at most one extra byte to distinguish an exact fit from growth.
        const auto maximum = std::min(chunk.size(), max_file_bytes - contents.size() + 1);
        try {
            input.read(chunk.data(), static_cast<std::streamsize>(maximum));
        } catch (const std::ios_base::failure &) {
            if (!input.eof() || input.bad()) {
                throw FileError("Cannot read file.");
            }
        }
        if (input.bad() || (input.fail() && !input.eof())) {
            throw FileError("Cannot read file.");
        }

        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > max_file_bytes - contents.size()) {
            throw FileError(size_limit_error);
        }
        if (count > expected_size - contents.size()) {
            throw FileError(size_changed_error);
        }
        contents.append(chunk.data(), count);
    } while (!input.eof());

    if (contents.size() != expected_size) {
        throw FileError(size_changed_error);
    }
    return contents;
}

std::string read_document_file(std::string_view path_text) {
    if (path_text.find('\0') != std::string_view::npos) {
        throw FileError("File paths cannot contain NUL bytes.");
    }
    const std::filesystem::path path(path_text);
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        throw FileError("File not found: " + std::string(path_text));
    }
    if (error) {
        throw FileError("Cannot inspect file: " + std::string(path_text));
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw FileError("Expected a regular file: " + std::string(path_text));
    }

    const auto expected = std::filesystem::file_size(path, error);
    if (error) {
        throw FileError("Cannot check file size: " + std::string(path_text));
    }
    if (expected > max_file_bytes) {
        throw FileError(size_limit_error);
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw FileError("Cannot open file: " + std::string(path_text));
    }

    auto contents = read_document_bytes(input, static_cast<std::size_t>(expected));
    if (std::filesystem::file_size(path, error) != expected || error) {
        throw FileError(size_changed_error);
    }
    return contents;
}

} // namespace minisearch::cli

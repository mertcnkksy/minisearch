#pragma once

#include <cstddef>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>

namespace minisearch::cli {

inline constexpr std::size_t max_file_bytes = 16'777'216;

class FileError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string read_document_bytes(std::istream &input, std::size_t expected_size);
[[nodiscard]] std::string read_document_file(std::string_view path);

} // namespace minisearch::cli

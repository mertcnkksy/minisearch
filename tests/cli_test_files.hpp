#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace minisearch::cli::test {

class TestFiles {
  public:
    explicit TestFiles(const std::filesystem::path &root = std::filesystem::temp_directory_path()) {
        std::random_device random;
        for (int attempt = 0; attempt < 32; ++attempt) {
            auto candidate = root / ("minisearch-cli-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                directory_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Could not create a unique CLI test directory.");
    }

    ~TestFiles() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    TestFiles(const TestFiles &) = delete;
    TestFiles &operator=(const TestFiles &) = delete;

    [[nodiscard]] const std::filesystem::path &directory() const noexcept { return directory_; }

    std::filesystem::path write(const std::filesystem::path &name,
                                std::string_view contents) const {
        auto path = directory_ / name;
        std::ofstream output(path, std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        if (!output) {
            throw std::runtime_error("Could not write CLI test file.");
        }
        return path;
    }

  private:
    std::filesystem::path directory_;
};

} // namespace minisearch::cli::test

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace minisearch::test {

struct ReferenceToken {
    std::string term;
    std::size_t begin;
    std::size_t end;
};

[[nodiscard]] bool is_ascii(std::string_view text);
[[nodiscard]] std::vector<ReferenceToken> reference_tokens(std::string_view text);

struct ReferenceScore {
    std::size_t id;
    long double score;
};

struct Frequencies {
    std::size_t documents = 0;
    std::size_t occurrences = 0;
};

class ReferenceSearch {
  public:
    std::size_t add(std::string_view document);
    [[nodiscard]] std::size_t documents() const noexcept { return documents_.size(); }
    [[nodiscard]] std::size_t terms() const noexcept { return frequencies_.size(); }
    [[nodiscard]] std::size_t tokens() const noexcept { return token_count_; }
    [[nodiscard]] long double average_length() const noexcept;
    [[nodiscard]] std::size_t length(std::size_t id) const { return documents_.at(id).size(); }
    [[nodiscard]] Frequencies frequencies(std::string_view term) const;
    [[nodiscard]] std::vector<std::size_t> positions(std::size_t id, std::string_view term) const;
    [[nodiscard]] std::vector<ReferenceScore> rank(std::string_view query, double k1 = 1.2,
                                                   double b = 0.75) const;

  private:
    std::vector<std::vector<ReferenceToken>> documents_;
    std::map<std::string, Frequencies, std::less<>> frequencies_;
    std::size_t token_count_ = 0;
};

[[nodiscard]] long double score_tolerance(long double score);

} // namespace minisearch::test

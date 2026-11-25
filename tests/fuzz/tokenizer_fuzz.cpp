#include "check.hpp"
#include "minisearch/text/tokenizer.hpp"
#include "reference_search.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *bytes, std::size_t size) {
    if (size > 4096) {
        return 0;
    }
    using minisearch::test::must;
    const std::string_view input(reinterpret_cast<const char *>(bytes), size);
    const bool valid = minisearch::test::is_ascii(input);
    try {
        const auto actual = minisearch::Tokenizer::tokenize(input);
        must(valid, "Tokenizer accepted a non-ASCII byte");
        const auto expected = minisearch::test::reference_tokens(input);
        must(actual.size() == expected.size(), "Token count");
        for (std::size_t i = 0; i < expected.size(); ++i) {
            must(actual[i].term == expected[i].term && actual[i].position == i &&
                     actual[i].start_offset == expected[i].begin &&
                     actual[i].end_offset == expected[i].end,
                 "Term, position or byte span differs");
        }
    } catch (const std::invalid_argument &) {
        must(!valid, "Tokenizer rejected valid ASCII");
    }
    return 0;
}

#include "minisearch/cli/repl.hpp"
#include "session_fixture.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <random>
#include <sstream>
#include <vector>

namespace minisearch::test {
namespace {

std::size_t output_operation(std::string_view expected, std::string_view actual,
                             const std::vector<std::size_t> &prompt_offsets) {
    const auto [difference, ignored] =
        std::mismatch(expected.begin(), expected.end(), actual.begin(), actual.end());
    const auto offset = static_cast<std::size_t>(difference - expected.begin());
    const auto next = std::upper_bound(prompt_offsets.begin(), prompt_offsets.end(), offset);
    return next == prompt_offsets.begin()
               ? 0
               : static_cast<std::size_t>(next - prompt_offsets.begin() - 1);
}

TEST(CliSessionIntegration, GeneratesOneHundredIndependentMixedSessions) {
    constexpr std::uint32_t seed = 0x009C110U;
    std::mt19937 random(seed);
    constexpr std::size_t sessions = 100;
    constexpr std::size_t commands = 50;
    for (std::size_t session = 0; session < sessions; ++session) {
        SessionFixture fixture;
        std::vector<std::size_t> prompt_offsets;
        prompt_offsets.reserve(commands + 2);
        for (std::size_t step = 0; step < commands; ++step) {
            prompt_offsets.push_back(fixture.output.size());
            const auto mode = random() % 9U;
            const auto term = "key" + std::to_string(random() % 12U);
            const auto document = "Common " + term + " common";
            switch (mode) {
            case 0:
            case 1:
                fixture.add("add " + document, document);
                break;
            case 2:
                fixture.add("add !?", "!?");
                break;
            case 3:
                fixture.error("add " + document + '\xFF',
                              "Non-ASCII byte at offset " + std::to_string(document.size()));
                break;
            case 4:
                fixture.error("search --top -1 " + term,
                              "Invalid result limit. Expected an unsigned decimal integer.");
                break;
            case 5:
                fixture.search("search --top 0 " + term, term, 0);
                break;
            case 6:
                fixture.search("search " + term + " common common", term + " common common", 10);
                break;
            case 7:
                fixture.search("search --top 2 -- " + term, term, 2);
                break;
            case 8:
                fixture.statistics();
                break;
            }
        }
        prompt_offsets.push_back(fixture.output.size());
        fixture.statistics();
        prompt_offsets.push_back(fixture.output.size());
        fixture.command(session % 2 == 0 ? "exit" : "quit");
        fixture.input += "add must not be processed\n";
        std::istringstream input(fixture.input);
        std::ostringstream output;
        std::ostringstream errors;
        SCOPED_TRACE(::testing::Message() << "seed=" << seed << " session=" << session);
        ASSERT_EQ(cli::run_repl(input, output, errors), 0);
        const auto actual = output.str();
        ASSERT_EQ(actual, fixture.output)
            << "First differing output at operation "
            << output_operation(fixture.output, actual, prompt_offsets) << "\nCommands in order:\n"
            << fixture.input;
        ASSERT_EQ(errors.str(), fixture.errors) << "Commands in order:\n" << fixture.input;
    }
}

} // namespace
} // namespace minisearch::test

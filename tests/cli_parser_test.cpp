#include "cli/command_parser.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using minisearch::cli::Command;
using minisearch::cli::parse_command;

TEST(CliParserTest, DispatchesOnlyLowercaseCommandsAndAllowsBlankLines) {
    EXPECT_EQ(parse_command("").command, Command::blank);
    EXPECT_EQ(parse_command(" \t \t").command, Command::blank);
    EXPECT_EQ(parse_command(" \thelp\t ").command, Command::help);
    EXPECT_EQ(parse_command("stats").command, Command::stats);
    EXPECT_EQ(parse_command("exit").command, Command::exit);
    EXPECT_EQ(parse_command("\tquit \t").command, Command::exit);
    for (const auto command : {"HELP", "searching", "help extra", "stats x", "exit x", "quit x",
                               "\vhelp", "stats\r", "add", "index \t", "search\t "}) {
        SCOPED_TRACE(command);
        EXPECT_THROW(static_cast<void>(parse_command(command)), std::invalid_argument);
    }
}

TEST(CliParserTest, PreservesPayloadAndOnlyTrimsPathEdges) {
    const auto add = parse_command(" \tadd\t \tSome  text\t with \"quotes\" \\n \t");
    EXPECT_EQ(add.command, Command::add);
    EXPECT_EQ(add.payload, "Some  text\t with \"quotes\" \\n \t");
    EXPECT_EQ(parse_command("add  !...").payload, "!...");
    const auto file = parse_command("index\t  root/folder with spaces/file.txt\t ");
    EXPECT_EQ(file.command, Command::index);
    EXPECT_EQ(file.payload, "root/folder with spaces/file.txt");
    EXPECT_EQ(parse_command("index \"file name\"").payload, "\"file name\"");
    EXPECT_EQ(parse_command("index $HOME/*.txt").payload, "$HOME/*.txt");
    const auto search = parse_command("search\t  python  database \t");
    EXPECT_EQ(search.command, Command::search);
    EXPECT_EQ(search.limit, 10U);
    EXPECT_EQ(search.payload, "python  database \t");
}

TEST(CliParserTest, ParsesOneTopOptionAndStopsAtTheTerminatorOrFirstQueryWord) {
    EXPECT_EQ(parse_command("search --top 5 python").limit, 5U);
    EXPECT_EQ(parse_command("search --top 0 python").limit, 0U);
    EXPECT_EQ(parse_command("search --top\t0001\tpython").limit, 1U);
    EXPECT_EQ(parse_command("search --top 10 python").limit, 10U);
    EXPECT_EQ(parse_command("search -- --top").payload, "--top");
    EXPECT_EQ(parse_command("search -- --top").limit, 10U);
    EXPECT_EQ(parse_command("search --top 5 -- --top").limit, 5U);
    EXPECT_EQ(parse_command("search --top 5 -- --top").payload, "--top");
    EXPECT_EQ(parse_command("search python --top 5").payload, "python --top 5");
    EXPECT_EQ(parse_command("search python --top 5").limit, 10U);
    EXPECT_EQ(parse_command("search --top 5 python --unknown").payload, "python --unknown");
    EXPECT_EQ(parse_command("search ...!?").payload, "...!?");
    EXPECT_EQ(parse_command("search -- ---").payload, "---");
    EXPECT_EQ(parse_command("search ---").payload, "---");
    EXPECT_EQ(parse_command("search -").payload, "-");

    for (const auto command :
         {"search --top=5 python", "search --unknown python", "search -x python",
          "search --top 5 --top 10 python", "search --top 5 --unknown python", "search --",
          "search --top 5", "search --top 5 -- \t", "search --top"}) {
        SCOPED_TRACE(command);
        EXPECT_THROW(static_cast<void>(parse_command(command)), std::invalid_argument);
    }
}

TEST(CliParserTest, AcceptsOnlyCompleteUnsignedDecimalValuesWithinSizeT) {
    constexpr auto largest = std::numeric_limits<std::size_t>::max();
    const auto maximum = std::to_string(largest);
    EXPECT_EQ(parse_command("search --top " + maximum + " python").limit, largest);
    EXPECT_THROW(static_cast<void>(parse_command("search --top " + maximum + "0 python")),
                 std::invalid_argument);
    for (const auto invalid : {"-1", "+5", "1.5", "5abc", "abc", "0x20", "--", "1\v", "\xFF"}) {
        SCOPED_TRACE(invalid);
        EXPECT_THROW(
            static_cast<void>(parse_command(std::string("search --top ") + invalid + " python")),
            std::invalid_argument);
    }
}

TEST(CliParserTest, RejectsEmbeddedNulInEveryCommandField) {
    for (const auto command :
         {std::string_view("help\0", 5), std::string_view("add te\0xt", 9),
          std::string_view("index name\0.txt", 15), std::string_view("search -- \0word", 15)}) {
        EXPECT_THROW(static_cast<void>(parse_command(command)), std::invalid_argument);
    }
}

} // namespace

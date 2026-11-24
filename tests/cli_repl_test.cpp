#include "minisearch/cli/repl.hpp"

#include "cli/command_parser.hpp"
#include "cli_test_files.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <ios>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

namespace {

using minisearch::cli::max_command_bytes;
using minisearch::cli::run_repl;
using minisearch::cli::test::TestFiles;

struct Session {
    int code;
    std::string output;
    std::string errors;
};

Session run(std::string_view commands) {
    std::istringstream input{std::string(commands)};
    std::ostringstream output;
    std::ostringstream errors;
    const auto code = run_repl(input, output, errors);
    return {code, output.str(), errors.str()};
}

std::size_t occurrences(std::string_view text, std::string_view part) {
    std::size_t count = 0;
    for (auto found = text.find(part); found != std::string_view::npos;
         found = text.find(part, found + part.size())) {
        ++count;
    }
    return count;
}

TEST(CliReplTest, PrintsTheHandCalculatedRankingAndCorpusStatistics) {
    const auto session = run("add Python Python Python\n"
                             "add Python supports data structures and algorithms\n"
                             "add Database systems\n"
                             "search --top 2 python\n"
                             "stats\nexit\n");
    EXPECT_EQ(session.code, 0);
    EXPECT_TRUE(session.errors.empty());
    EXPECT_NE(session.output.find("Indexed document 0.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Indexed document 1.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Indexed document 2.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Rank  Doc ID  Score\n"
                                  "1     0       0.768519\n"
                                  "2     1       0.372921\n\n"
                                  "Returned 2 results.\n"),
              std::string::npos);
    EXPECT_NE(session.output.find("Documents:      3\nUnique terms:   8\n"
                                  "Total tokens:   11\nAvg. length:    3.666667\n"),
              std::string::npos);
}

TEST(CliReplTest, SupportsDefaultCustomZeroAndMaximumResultLimitsWithExactTies) {
    std::string commands;
    for (int i = 0; i < 12; ++i) {
        commands += "add Python\n";
    }
    commands += "search python\nsearch --top 1 PYTHON\nsearch --top 0 python\n";
    commands += "search --top " + std::to_string(std::numeric_limits<std::size_t>::max()) +
                " -- python\nsearch unknown\nsearch ...\nexit\n";
    const auto session = run(commands);
    EXPECT_EQ(session.code, 0);
    EXPECT_TRUE(session.errors.empty());
    EXPECT_NE(session.output.find("Returned 10 results.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Returned 1 result.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Returned 12 results.\n"), std::string::npos);
    EXPECT_EQ(occurrences(session.output, "No results.\n"), 3U);
    EXPECT_EQ(occurrences(session.output, "1     0       0.039221"), 3U);
    EXPECT_NE(session.output.find("12     11       0.039221\n"), std::string::npos);
}

TEST(CliReplTest, RecoversFromValidationErrorsWithoutChangingCorpusOrConsumingIds) {
    std::string commands = "add hello\nstats\nsearch --top -1 hello\n"
                           "add bad\xFF\nsearch \xFF\nsearch --top 0 \xFF\n";
    commands.append("add nul\0bytes\n", 14);
    commands += "nonesuch\nadd \t\nstats\nadd !?\nsearch hello\nstats\nexit\n";
    const auto session = run(commands);
    EXPECT_EQ(session.code, 0);
    EXPECT_EQ(occurrences(session.errors, "Error: "), 7U);
    EXPECT_EQ(occurrences(session.errors, "Non-ASCII byte"), 3U);
    EXPECT_NE(session.errors.find("Invalid result limit."), std::string::npos);
    EXPECT_NE(session.errors.find("cannot contain NUL"), std::string::npos);
    EXPECT_NE(session.errors.find("Unknown command: nonesuch"), std::string::npos);
    EXPECT_NE(session.errors.find("Usage: add <text>"), std::string::npos);
    EXPECT_EQ(occurrences(session.output, "Documents:      1\n"), 2U);
    EXPECT_NE(session.output.find("Documents:      2\nUnique terms:   1\n"
                                  "Total tokens:   1\nAvg. length:    0.500000\n"),
              std::string::npos);
    EXPECT_EQ(occurrences(session.output, "Indexed document "), 2U);
    EXPECT_NE(session.output.find("Indexed document 1.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Returned 1 result.\n"), std::string::npos);
}

TEST(CliReplTest, IndexesWholeFilesAndRecoversFromMissingAndInvalidFiles) {
    const TestFiles files;
    const auto invalid = files.write("not ascii.txt", "word \xFF");
    const auto empty = files.write("empty.txt", "");
    const auto binary =
        files.write("file with spaces.txt", std::string_view("alpha\0beta\r\nalpha", 17));
    const auto session =
        run("index " + (files.directory() / "missing.txt").string() + '\n' + "index " +
            files.directory().string() + '\n' + "index " + invalid.string() + '\n' + "index " +
            empty.string() + '\n' + "index \t" + binary.string() +
            " \t\n"
            "search beta\nstats\nexit\n");
    EXPECT_EQ(session.code, 0);
    EXPECT_EQ(occurrences(session.errors, "Error: "), 3U);
    EXPECT_NE(session.errors.find("File not found"), std::string::npos);
    EXPECT_NE(session.errors.find("Expected a regular file"), std::string::npos);
    EXPECT_NE(session.errors.find("Non-ASCII byte"), std::string::npos);
    EXPECT_EQ(occurrences(session.output, "Indexed document "), 2U);
    EXPECT_NE(session.output.find("Indexed document 0.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Indexed document 1.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Rank  Doc ID  Score\n1     1"), std::string::npos);
    EXPECT_NE(session.output.find("Documents:      2\nUnique terms:   2\n"
                                  "Total tokens:   3\nAvg. length:    1.500000\n"),
              std::string::npos);
}

TEST(CliReplTest, PrintsHelpAndStopsAtExitOrQuitAndKeepsSessionsIndependent) {
    const auto first = run("\r\n \t\r\nhelp\r\nadd hello\r\nquit\r\nadd ignored\n");
    EXPECT_EQ(first.code, 0);
    EXPECT_TRUE(first.errors.empty());
    EXPECT_TRUE(first.output.starts_with("MiniSearch\nIn-memory document search engine.\n"));
    for (const auto line : {"add <text>", "index <path>", "search [--top K] [--] <query>", "stats",
                            "help", "exit", "quit"}) {
        EXPECT_NE(first.output.find(std::string("  ") + line + '\n'), std::string::npos);
    }
    EXPECT_EQ(occurrences(first.output, "Indexed document"), 1U);
    const auto second = run("search hello\nstats\nexit\nadd ignored\n");
    EXPECT_EQ(second.code, 0);
    EXPECT_TRUE(second.errors.empty());
    EXPECT_NE(second.output.find("No results.\n"), std::string::npos);
    EXPECT_NE(second.output.find("Documents:      0\nUnique terms:   0\n"
                                 "Total tokens:   0\nAvg. length:    0.000000\n"),
              std::string::npos);
    EXPECT_EQ(second.output.find("Indexed document"), std::string::npos);
}

TEST(CliReplTest, HandlesEmptyInputAndAnUnterminatedFinalCommandExactlyOnce) {
    const auto empty = run("");
    EXPECT_EQ(empty.code, 0);
    EXPECT_TRUE(empty.errors.empty());
    EXPECT_EQ(occurrences(empty.output, "minisearch> "), 1U);
    const auto final = run("add hello\nstats");
    EXPECT_EQ(final.code, 0);
    EXPECT_TRUE(final.errors.empty());
    EXPECT_EQ(occurrences(final.output, "Documents:      1\n"), 1U);
    EXPECT_EQ(occurrences(final.output, "minisearch> "), 2U);
}

std::string sized_add(std::size_t bytes) {
    std::string command = "add .";
    command.resize(bytes, ' ');
    return command;
}

TEST(CliReplTest, BoundsLineReadingIncludingCrLfAndDiscardsOversizedTails) {
    const auto min = sized_add(max_command_bytes - 1);
    const auto exact = sized_add(max_command_bytes);
    const auto over = sized_add(max_command_bytes + 1);
    const auto session = run(min + '\n' + exact + '\n' + min + "\r\n" + exact + "\r\n" + over +
                             "add tail must be discarded\nadd next\nstats\nexit\n");
    EXPECT_EQ(session.code, 0);
    EXPECT_EQ(occurrences(session.errors, "Error: Command exceeds the 1 MiB byte limit."), 2U);
    EXPECT_EQ(occurrences(session.output, "Indexed document "), 4U);
    EXPECT_NE(session.output.find("Indexed document 3.\n"), std::string::npos);
    EXPECT_NE(session.output.find("Total tokens:   1\n"), std::string::npos);

    const auto exact_eof = run(exact);
    EXPECT_EQ(exact_eof.code, 0);
    EXPECT_TRUE(exact_eof.errors.empty());
    EXPECT_EQ(occurrences(exact_eof.output, "Indexed document 0."), 1U);
    const auto over_eof = run(over + "add ignored");
    EXPECT_EQ(over_eof.code, 0);
    EXPECT_EQ(occurrences(over_eof.errors, "Error:"), 1U);
    EXPECT_EQ(over_eof.output.find("Indexed document"), std::string::npos);
}

TEST(CliReplTest, EscapesControlBytesOnlyInTerminalDiagnostics) {
    const auto session = run("bad\x1B[31m\x7F\r\v\x9B\n"
                             "search --bad\x1B[0m\n"
                             "index missing\x1B[5m\r.txt\n"
                             "add alpha\x1B!beta\nsearch alpha\x1B!beta\nexit\n");
    EXPECT_EQ(session.code, 0);
    EXPECT_NE(session.errors.find("bad\\x1B[31m\\x7F\\x0D\\x0B\\x9B"), std::string::npos);
    EXPECT_NE(session.errors.find("--bad\\x1B[0m"), std::string::npos);
    EXPECT_NE(session.errors.find("missing\\x1B[5m\\x0D.txt"), std::string::npos);
    EXPECT_EQ(session.errors.find_first_of("\x1B\x7F\r\v\x9B"), std::string::npos);
    EXPECT_NE(session.output.find("Returned 1 result."), std::string::npos);
}

class CommaDecimal : public std::numpunct<char> {
  protected:
    char do_decimal_point() const override { return ','; }
};

TEST(CliReplTest, FormatsNumbersWithoutUsingOrChangingOutputStreamSettings) {
    std::istringstream input("add Python\nsearch Python\nstats\nexit\n");
    std::ostringstream output;
    std::ostringstream errors;
    const std::locale locale(std::locale::classic(), new CommaDecimal);
    output.imbue(locale);
    output.setf(std::ios::hex, std::ios::basefield);
    output.setf(std::ios::scientific, std::ios::floatfield);
    output.setf(std::ios::showpos);
    output.precision(2);
    output.width(25);
    const auto flags = output.flags();

    EXPECT_EQ(run_repl(input, output, errors), 0);
    EXPECT_TRUE(errors.str().empty());
    EXPECT_NE(output.str().find("1     0       0.287682"), std::string::npos);
    EXPECT_NE(output.str().find("Avg. length:    1.000000"), std::string::npos);
    EXPECT_EQ(output.getloc(), locale);
    EXPECT_EQ(output.flags(), flags);
    EXPECT_EQ(output.precision(), 2);
    EXPECT_EQ(output.width(), 25);
}

TEST(CliReplTest, HandlesEofAndOversizeRecoveryWithStreamExceptionMasks) {
    for (const auto &commands : {std::string{}, sized_add(max_command_bytes + 1),
                                 sized_add(max_command_bytes + 1) + "\nstats"}) {
        std::istringstream input(commands);
        input.exceptions(std::ios::eofbit | std::ios::failbit | std::ios::badbit);
        std::ostringstream output;
        std::ostringstream errors;
        EXPECT_EQ(run_repl(input, output, errors), 0);
        EXPECT_EQ(occurrences(errors.str(), "Error:"), commands.empty() ? 0U : 1U);
        if (commands.ends_with("stats")) {
            EXPECT_EQ(occurrences(output.str(), "Documents:      0"), 1U);
        }
    }
}

class BrokenRead : public std::streambuf {
  public:
    explicit BrokenRead(std::string prefix = {}) : prefix_(std::move(prefix)) {
        auto *begin = prefix_.data();
        setg(begin, begin, begin + prefix_.size());
    }

  protected:
    int_type underflow() override { throw std::ios_base::failure("unreadable input"); }

  private:
    std::string prefix_;
};

class AllocationFailure : public std::streambuf {
  protected:
    int_type underflow() override { throw std::bad_alloc(); }
};

class UnsafeReadError : public std::streambuf {
  protected:
    int_type underflow() override { throw std::runtime_error("unsafe\x1B\r\n"); }
};

TEST(CliReplTest, ReadFailureIsFatalWithAndWithoutStreamExceptions) {
    for (const auto mask : {std::ios::goodbit, std::ios::badbit}) {
        BrokenRead buffer;
        std::istream input(&buffer);
        input.exceptions(mask);
        std::ostringstream output;
        std::ostringstream errors;
        EXPECT_EQ(run_repl(input, output, errors), 1);
        EXPECT_NE(errors.str().find("Fatal: Input stream failure."), std::string::npos);
        EXPECT_EQ(occurrences(output.str(), "minisearch> "), 1U);
    }
    std::istringstream input;
    input.setstate(std::ios::failbit);
    std::ostringstream output;
    std::ostringstream errors;
    EXPECT_EQ(run_repl(input, output, errors), 1);
    EXPECT_NE(errors.str().find("Fatal: Input stream failure."), std::string::npos);

    BrokenRead overlong(sized_add(max_command_bytes + 16));
    std::istream broken_drain(&overlong);
    std::ostringstream drained_output;
    std::ostringstream drain_errors;
    EXPECT_EQ(run_repl(broken_drain, drained_output, drain_errors), 1);
    EXPECT_EQ(drained_output.str().find("Indexed document"), std::string::npos);
    EXPECT_EQ(drain_errors.str(), "Fatal: Input stream failure.\n");
}

TEST(CliReplTest, ReportsAllocationFailureAndEscapesUnexpectedExceptionMessages) {
    AllocationFailure allocation;
    UnsafeReadError unsafe;
    for (auto *buffer :
         {static_cast<std::streambuf *>(&allocation), static_cast<std::streambuf *>(&unsafe)}) {
        std::istream input(buffer);
        input.exceptions(std::ios::badbit);
        std::ostringstream output;
        std::ostringstream errors;
        EXPECT_EQ(run_repl(input, output, errors), 1);
        const auto expected =
            buffer == &allocation ? "Fatal: Out of memory.\n" : "Fatal: unsafe\\x1B\\x0D\\x0A\n";
        EXPECT_EQ(errors.str(), expected);
        EXPECT_EQ(output.str().find("Indexed document"), std::string::npos);
    }
}

class BrokenFlush : public std::stringbuf {
  public:
    explicit BrokenFlush(int fail_on) : fail_on_(fail_on) {}

  protected:
    int sync() override { return ++flushes_ == fail_on_ ? -1 : 0; }

  private:
    int fail_on_;
    int flushes_ = 0;
};

class BrokenCommandWrite : public std::stringbuf {
  protected:
    std::streamsize xsputn(const char *text, std::streamsize count) override {
        if (std::string_view(text, static_cast<std::size_t>(count))
                .starts_with("Indexed document ")) {
            throw std::invalid_argument("cannot write");
        }
        return std::stringbuf::xsputn(text, count);
    }
};

TEST(CliReplTest, StopsBeforeReadingTheNextCommandWhenOutputFlushFails) {
    for (const auto mask : {std::ios::goodbit, std::ios::badbit}) {
        for (const auto fail_on : {1, 2}) {
            std::istringstream input("add hello\nstats\n");
            BrokenFlush buffer(fail_on);
            std::ostream output(&buffer);
            output.exceptions(mask);
            std::ostringstream errors;
            EXPECT_EQ(run_repl(input, output, errors), 1);
            EXPECT_NE(errors.str().find("Fatal:"), std::string::npos);
            EXPECT_EQ(input.peek(), fail_on == 1 ? 'a' : 's');
            EXPECT_EQ(buffer.str().find("Documents:"), std::string::npos);
        }
    }
    BrokenCommandWrite buffer;
    std::ostream output(&buffer);
    output.exceptions(std::ios::badbit);
    std::istringstream input("add hello\nstats\n");
    std::ostringstream errors;
    EXPECT_EQ(run_repl(input, output, errors), 1);
    EXPECT_EQ(errors.str(), "Fatal: Output stream failure.\n");
    EXPECT_EQ(input.peek(), 's');
}

TEST(CliReplTest, DetectsFinalFlushFailureAndSurvivesASecondErrorWhileReporting) {
    std::istringstream input("exit\n");
    BrokenFlush output_buffer(2);
    BrokenFlush error_buffer(1);
    std::ostream output(&output_buffer);
    std::ostream errors(&error_buffer);
    errors.exceptions(std::ios::badbit);
    EXPECT_EQ(run_repl(input, output, errors), 1);

    std::istringstream other("bad command\nadd ignored\n");
    std::ostringstream other_output;
    BrokenFlush other_errors_buffer(1);
    std::ostream other_errors(&other_errors_buffer);
    EXPECT_EQ(run_repl(other, other_output, other_errors), 1);
    EXPECT_EQ(other.peek(), 'a');
}

} // namespace

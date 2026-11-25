#include "cli/command_parser.hpp"
#include "minisearch/cli/repl.hpp"

#include <gtest/gtest.h>

#include <new>
#include <sstream>

namespace {

thread_local bool fail_buffer = false;
thread_local std::size_t buffer_failures = 0;

} // namespace

// Only replace the ordinary array family in this isolated executable.
void *operator new[](std::size_t bytes) {
    if (fail_buffer && bytes == minisearch::cli::max_command_bytes + 1) {
        ++buffer_failures;
        throw std::bad_alloc{};
    }
    return ::operator new(bytes);
}

void operator delete[](void *address) noexcept { ::operator delete(address); }
void operator delete[](void *address, std::size_t) noexcept { ::operator delete(address); }

namespace {

TEST(CliAllocationIntegration, InjectedCommandBufferAllocationFailureIsFatalAndAllowsNewSession) {
    std::istringstream input("add hello\nexit\n");
    std::ostringstream output;
    std::ostringstream errors;
    buffer_failures = 0;
    fail_buffer = true;
    const auto exit_code = minisearch::cli::run_repl(input, output, errors);
    fail_buffer = false;
    EXPECT_EQ(exit_code, 1);
    EXPECT_EQ(buffer_failures, 1U);
    EXPECT_EQ(input.peek(), 'a');
    EXPECT_TRUE(output.str().empty());
    EXPECT_EQ(errors.str(), "Fatal: Out of memory.\n");

    std::ostringstream retry_errors;
    EXPECT_EQ(minisearch::cli::run_repl(input, output, retry_errors), 0);
    EXPECT_TRUE(retry_errors.str().empty());
    EXPECT_NE(output.str().find("Indexed document 0.\n"), std::string::npos);
}

} // namespace

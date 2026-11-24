#pragma once

#include <iosfwd>

namespace minisearch::cli {

// Runs one in-memory session. Returns 0 on exit or EOF and 1 on a fatal error.
int run_repl(std::istream &input, std::ostream &output, std::ostream &errors);

} // namespace minisearch::cli

#pragma once

#include <cstdio>
#include <cstdlib>

namespace minisearch::test {

inline void must(bool condition, const char *message) {
    if (!condition) {
        std::fputs(message, stderr);
        std::fputc('\n', stderr);
        std::abort();
    }
}

} // namespace minisearch::test

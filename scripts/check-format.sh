#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
find include src tests -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 |
    xargs -0 "${CLANG_FORMAT:-clang-format}" --dry-run --Werror

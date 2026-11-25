# Testing and release readiness

## Test architecture

The original suites cover the data structures, ASCII tokenization, positional index and
statistics, BM25, Top-K, parser and file reader, REPL lifecycle, allocation and numeric
boundaries, and executable smoke behavior. The integration suites add stateful checks across
those boundaries, independent arithmetic from raw input, full executable sessions over a
shared corpus, and tests of failure paths that previously used temporary probes.

The C++ oracle in `tests/integration/reference_search.cpp` scans raw ASCII using a lookup
alphabet, keeps its own tokens and positions, calculates TF, DF, CF and corpus statistics,
and brute-force scores documents in long double. A weighted reciprocal computes the TF factor
without multiplying a large k1 by TF or normalized length. The oracle deduplicates query terms
and full-sorts results independently. It links only into test and fuzz executables.

The ordered, 24-document manifest in `tests/fixtures/golden_corpus.hpp` fixes document IDs by
insertion order and includes a duplicate, an empty document, rare and common terms, and
different lengths and frequencies. Executable fixtures write it into build-local files and
calculate expected CLI output from the oracle. The manifest and synthetic corpus check
ranking correctness. Retrieval effectiveness requires representative corpora and judgments.

Complete candidate sets are compared by ID and numerical score before checking Top-K.
For these short ASCII queries the comparison uses an absolute budget of eight double epsilons
plus 24 double epsilons per unit of score for normalization, logarithm, and positive summation.
The independent mathematical order is checked outside close-score intervals. A separate
full sort of production doubles checks exact Top-K order and membership even inside those
intervals. There are explicit mathematical tie and near-cutoff cases. Results are tested
within each floating-point environment.

The stateful core test runs 64 independent sessions with 160 operations each using seed
`0xA990010`. The REPL test runs 100 sessions with 50 generated commands plus stats and exit
per session using seed `0x009C110`. Operation choices use explicit modulo mappings of
`mt19937` outputs. Failure messages include the seed and operation or complete session input.
Metamorphic checks exercise case, order, duplicates, separator changes, unknown query terms,
Top-K prefixes, repeatability, and corpus changes. CLI executable cases exercise LF, CRLF,
unterminated EOF, process isolation, file paths, and recoverable errors.

Allocation overrides are isolated in their own test processes. The hash case injects both
bucket allocation and an existing-key copy failure during same-capacity cleanup. The REPL
test fails the real command buffer allocation. Linux tests check closed stdout, dangling links,
Unix sockets, and permission denial. Permission denial is explicitly skipped if privileges
bypass read permissions. Smoke subprocesses and new suites have CTest timeouts.

## Full suite and focused checks

Run from the repository root. CMake 3.24+, Ninja, and a C++20 compiler are required.
CMake fetches a checksum-pinned GoogleTest when testing is enabled. Runtime fixtures are
local and offline.

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
ctest --test-dir build/debug -L integration --output-on-failure --no-tests=error
ctest --test-dir build/debug -L allocation --output-on-failure --no-tests=error
./build/debug/minisearch --help
./build/debug/minisearch --version
```

## Clean compiler matrix and static analysis

```bash
bash <<'BASH'
set -euo pipefail
gccxx="$(command -v g++-15 || command -v g++)"
clangxx="$(command -v clang++-21 || command -v clang++)"
tidy="$(command -v clang-tidy-21 || command -v clang-tidy)"
formatter="$(command -v clang-format-21 || command -v clang-format)"
GCC_DIR="$(dirname "$("$gccxx" -print-libgcc-file-name)")"
work="$(mktemp -d)"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

check() {
    local name="$1" compiler="$2" preset="$3"
    shift 3
    local dir="$work/$name-$preset"
    cmake --preset "$preset" -B "$dir" "-DCMAKE_CXX_COMPILER=$compiler" "$@"
    cmake --build "$dir" --parallel 2
    ctest --test-dir "$dir" --output-on-failure --no-tests=error
}
for preset in debug release sanitizers; do
    check gcc "$gccxx" "$preset"
    check clang "$clangxx" "$preset" "-DCMAKE_CXX_FLAGS=--gcc-install-dir=$GCC_DIR"
done
CLANG_FORMAT="$formatter" scripts/check-format.sh
check tidy "$clangxx" debug "-DCMAKE_CXX_FLAGS=--gcc-install-dir=$GCC_DIR" \
    -DMINISEARCH_ENABLE_CLANG_TIDY=ON "-DMINISEARCH_CLANG_TIDY=$tidy"
git diff --check
printf 'Builds and test logs: %s\n' "$work"
BASH
```

## Fuzzing

Configure and link a dedicated Clang fuzz build. The tokenizer and bounded search pipeline
use copies of core sources compiled with `fuzzer-no-link,address,undefined`. The parser
harness uses an equally instrumented parser object. Only fuzz executables link the libFuzzer
entry point. Each run uses a writable copy of its original seed fixtures.

```bash
bash <<'BASH'
set -euo pipefail
clangxx="$(command -v clang++-21 || command -v clang++)"
GCC_DIR="$(dirname "$(g++ -print-libgcc-file-name)")"
work="$(mktemp -d)"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
cmake --preset release -B "$work/build" "-DCMAKE_CXX_COMPILER=$clangxx" \
    "-DCMAKE_CXX_FLAGS=--gcc-install-dir=$GCC_DIR" \
    -DBUILD_TESTING=OFF -DMINISEARCH_ENABLE_FUZZING=ON
cmake --build "$work/build" --parallel 2
printf 'Fuzz logs and corpora: %s\n' "$work"
for target in tokenizer command_parser search_pipeline; do
    mkdir "$work/$target" "$work/$target-artifacts"
    cp -R "tests/fuzz/corpus/$target/." "$work/$target/"
    max_len=4096
    if [[ "$target" == search_pipeline ]]; then max_len=1024; fi
    timeout 75s "$work/build/tests/fuzz/minisearch_${target}_fuzz" "$work/$target" \
        -seed=9009 -max_len="$max_len" -max_total_time=60 -timeout=5 \
        -rss_limit_mb=768 "-artifact_prefix=$work/$target-artifacts/" \
        2>&1 | tee "$work/$target.log"
done
BASH
```

To reproduce a failure, pass its saved artifact path directly to the corresponding
`minisearch_*_fuzz` executable with the same sanitizer environment. Keep generated corpora
and crash artifacts outside the repository. The search harness accepts at most eight
96-byte documents and a 192-byte query. The parser harness never opens a path.

## Coverage

Use a matching GCC and gcov from the same toolchain. Build and test in a fresh directory
before collecting the report.

```bash
bash <<'BASH'
set -euo pipefail
gccxx="$(command -v g++-15 || command -v g++)"
gcov="$(command -v gcov-15 || command -v gcov)"
work="$(mktemp -d)"
cmake --preset debug -B "$work" "-DCMAKE_CXX_COMPILER=$gccxx" -DCMAKE_CXX_FLAGS=--coverage
cmake --build "$work" --parallel 2
ctest --test-dir "$work" --output-on-failure --no-tests=error
python3 scripts/coverage-report.py "$work" --gcov "$gcov"
BASH
```

For a baseline comparison, build the MS-008 merge in an isolated source tree with the same
compiler and instrumentation. The current working tree is left intact.

```bash
bash <<'BASH'
set -euo pipefail
gccxx="$(command -v g++-15 || command -v g++)"
gcov="$(command -v gcov-15 || command -v gcov)"
work="$(mktemp -d)"
mkdir "$work/source"
git archive 9fe4566118e4bbc9bdf5922c911ddff0ae0e5ff6 | tar -x -C "$work/source"
cmake -S "$work/source" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    "-DCMAKE_CXX_COMPILER=$gccxx" -DCMAKE_CXX_FLAGS=--coverage
cmake --build "$work/build" --parallel 2
ctest --test-dir "$work/build" --output-on-failure --no-tests=error
python3 scripts/coverage-report.py "$work/build" --source "$work/source" --gcov "$gcov"
BASH
```

The report includes production source and instantiated header-only code, excludes third-party and test files,
and merges repeated line records where possible. Branches count taken gcov edges, including
exception edges. Explicit throw lines give a narrow error-path measure. New template
instantiations can change denominators, so inspect counts as well as percentages.
Fuzzer edge counts are a separate metric.

Run the full CTest suite before collecting a report. The reporter fails on missing,
empty, or unreadable profile data and on gcov diagnostics.

## Release build with testing disabled

```bash
bash <<'BASH'
set -euo pipefail
work="$(mktemp -d)"
cmake --preset release -B "$work/build" -DBUILD_TESTING=OFF \
    -DMINISEARCH_ENABLE_FUZZING=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build "$work/build" --parallel 2
test ! -e "$work/build/_deps"
"$work/build/minisearch" --help
test "$("$work/build/minisearch" --version)" = 'minisearch 0.1.0-dev'
mkdir "$work/documents"
printf 'Database systems\n' > "$work/documents/database notes.txt"
cd "$work"
build/minisearch <<'COMMANDS'
add Python Python Python
add Python supports data structures and algorithms
index documents/database notes.txt
search --top 2 python
stats
exit
COMMANDS
BASH
```

## CI and release decision

The pinned GitHub Actions workflow runs the GCC and Clang matrix with the normal CTest
suite, so the permanent integration tests run with the regression tests. Fuzz campaigns
are explicit opt-in runs. Verify the hosted CI run separately from local checks.

Readiness for MS-010 requires the existing regression tests, new integration and oracle
checks, stateful and metamorphic checks, executable tests, compiler and sanitizer matrix,
static analysis, formatting, testing-disabled build, and README demo. It also requires
no open P0 or P1 finding. Record optional coverage, fuzz and platform gaps explicitly.
This local decision supports the next milestone. Publication still requires review and CI.

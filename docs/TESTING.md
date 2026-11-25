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
The MS-009 run covered 10,240 core operations, 3,354 independent candidate comparisons,
and 3,491 exact Top-K comparisons. The 100 REPL sessions generated 5,000 commands.
There is also a deterministic 10,000-document test. These checks run inside CTest cases.
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
local and offline. On Linux the complete suite has 163 CTest cases, with 12 cases
under the `integration` label and 13 under `allocation`. Platform-specific cases
may be unavailable or report skipped. Record those separately from passes.

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
ctest --test-dir build/debug -L integration --output-on-failure --no-tests=error
ctest --test-dir build/debug -L allocation --output-on-failure --no-tests=error
./build/debug/minisearch --help
test "$(./build/debug/minisearch --version)" = 'minisearch 0.1.0'
```

## Clean compiler matrix and static analysis

These Bash commands use GCC 15 and LLVM 21 when installed with versioned names.
The Clang configuration explicitly selects the GCC installation so Clang can
find the matching libstdc++ headers and libraries.

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

## Fuzz smoke verification

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
    timeout 25s "$work/build/tests/fuzz/minisearch_${target}_fuzz" "$work/$target" \
        -seed=9009 -max_len="$max_len" -max_total_time=10 -timeout=5 \
        -rss_limit_mb=768 "-artifact_prefix=$work/$target-artifacts/" \
        2>&1 | tee "$work/$target.log"
done
BASH
```

To reproduce a failure, pass its saved artifact path directly to the corresponding
`minisearch_*_fuzz` executable with the same sanitizer environment. Keep generated corpora
and crash artifacts outside the repository. The search harness accepts at most eight
96-byte documents and a 192-byte query. The parser harness never opens a path.

The historical MS-009 campaign used `-max_total_time=60` with a 75-second outer
timeout. It recorded 489,576 tokenizer, 511,512 parser, and 208,096 search-pipeline
executions without a crash or sanitizer finding. These execution counts depend
on the runtime environment. A bounded fuzz run cannot establish the absence of defects.

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
Counts depend on the compiler, build configuration, and instrumentation. Fuzzer edge
counts are a separate metric.

The historical MS-009 GCC/gcov run measured 406/410 functions, 795/816 lines,
1,300/1,911 taken branch edges, and 38/49 explicit throw lines.

Run the full CTest suite before collecting a report. The reporter fails on missing,
empty, or unreadable profile data and on gcov diagnostics.

## Release build with testing disabled

The [Quick Start](../README.md#quick-start) uses the dedicated `release-cli` preset.
This verification uses a fresh build directory, disables FetchContent network
access, and checks the literal version independently of CMake's project variable.

```bash
bash <<'BASH'
set -euo pipefail
work="$(mktemp -d)"
cmake --preset release-cli -B "$work/build" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build "$work/build" --parallel 2
test ! -e "$work/build/_deps"
"$work/build/minisearch" --help
test "$("$work/build/minisearch" --version)" = 'minisearch 0.1.0'
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
suite and runs formatting and clang-tidy checks. Fuzz campaigns are explicit opt-in
runs. Check the hosted CI results for the release PR and the exact merged commit.

### MS-010A release preparation

Review the source and documentation, run the full compiler matrix and static
checks, and verify the testing-disabled Release build, CLI transcript, exact
version, C++ examples, documentation links, and fuzz smoke runs. Record platform
skips, any unrun checks, and remaining findings. P0 or P1 findings block release
readiness. Local verification supports opening a release PR after approval.

### MS-010B publication checklist

1. Review the MS-010A diff and required local checks. Obtain approval to commit,
   push `feat/ms-010`, and open a PR targeting `main`.
2. Verify the hosted CI results and complete the PR review before merging.
3. Get a fresh checkout of merged `main`, rerun final verification, and record
   the exact commit and its `minisearch 0.1.0` output.
4. Use read-only queries to check that the remote `v0.1.0` tag and GitHub
   Release are both available for first publication. Resolve any existing
   record or access problem before proceeding.
5. With publication approval, create an annotated `v0.1.0` tag at that verified
   `main` commit. Verify its target and push only that tag.
6. Publish the GitHub Release using the reviewed English notes. Verify the
   published date and metadata, the tag and commit, and the tag-based source archives.

The v0.1.0 distribution is a source release. The source archive includes the
project's MIT license. Test builds fetch GoogleTest (BSD 3-Clause) separately.
GitHub Actions and compiler tooling are used for verification. Any binary or
third-party redistribution requires its own review.

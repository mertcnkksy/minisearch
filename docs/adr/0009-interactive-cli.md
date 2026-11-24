# ADR 0009 — Interactive CLI

- Status: Accepted

## Context and decision

The MVP exposes in-memory indexing and ranked retrieval through an interactive terminal
session. The CLI calls the existing core APIs and owns parsing, files, streams, and display.

## REPL architecture and session lifecycle

`minisearch_cli` compiles the parser, file reader, and REPL. The executable handles startup
arguments and invokes `run_repl(input, output, errors)` with standard streams. Each call owns a
fresh InvertedIndex. The index is destroyed on exit, EOF, or a fatal error. The prompt is
flushed and checked before input is read, and normal exit checks the final flush.

## Command grammar and input validation

| Command | Effect |
| --- | --- |
| `add <text>` | Index the supplied ASCII text as a new document. |
| `index <path>` | Index one whole plain-text file. |
| `search [--top K] [--] <query>` | Return up to K BM25 matches. The default is 10. |
| `stats` | Display document count, distinct term count, total tokens, and average document length. |
| `help` | Describe commands and session storage. |
| `exit`, `quit` | Close the session. |

Command names are lowercase and case-sensitive. ASCII space and tab separate the first word
and its payload. Blank lines are ignored. Internal and trailing bytes of text and queries are
preserved. Index paths trim leading and trailing ASCII spaces and tabs, so names with these
edge characters are unsupported. Internal path spaces are preserved. Paths are literal and
receive no quote, wildcard, variable, or escape expansion.

`--top` is allowed once at the beginning. K is a complete unsigned decimal `size_t` value,
including zero and `SIZE_MAX`. Signs, overflow, and partial numbers are rejected. `--` ends
option parsing. A word beginning with `-` and containing an ASCII letter or digit in the
option position is treated as an option. After the first regular query word, all remaining
bytes are query text. Empty text, query, and path arguments are usage errors. Punctuation-only
text and queries are accepted. To use the reserved `--` as the query, write `search -- --`.

NUL bytes are rejected in command lines. LF terminates a line. All bytes preceding it count
toward the limit, including CR in CRLF. The reader strips that CR after counting. An
unterminated final line is processed once.

## File indexing and resource limits

- The CLI reads into a reusable **1,048,576 byte** command buffer plus a terminator.
  It discards an oversized line through LF or EOF and emits one error for that line.
- `index` checks regular file status and a **16,777,216 byte** limit before opening in
  binary mode. Symbolic links to regular files are followed. Missing paths, directories,
  special files, and embedded NUL in paths are rejected. Paths use the platform's native
  narrow path encoding.
- The reader checks the starting size and reads in fixed chunks. It checks the actual byte
  count and uses at most one extra byte to detect over-limit growth. The final metadata size
  must still match. A read failure or detected size change prevents insertion. An empty file
  produces an empty document. The tokenizer handles NUL separators and rejects non-ASCII
  document bytes according to its existing contract.

These checks assume a trusted local file system. A portable status check before opening has
a time-of-check/time-of-use window. The reader can detect size discrepancies but cannot
detect every replacement or same-size modification. File size limits cover CLI input bytes.
Tokenization, the index, and retrieval can use additional memory.

## Search integration and output

The CLI invokes the MS-007 `search` API with the parsed query and limit. The core validates
ASCII even for `K = 0` and applies the standard BM25 configuration. The output keeps the
ranking of the returned values. Exact score ties use ascending document ID.

Scores and average document length use six fixed decimal places. Conversion uses
`std::to_chars`, preserving locale, precision, flags, and width of the output stream.
Integers retain their original widths. The displayed result count is the number returned.
Empty searches print `No results.`. Stats come directly from the index.

Command results go to stdout. Diagnostics go to stderr. Untrusted diagnostic bytes outside
printable ASCII use `\xHH` notation, and literal backslashes are escaped. This applies to
arguments, command names, options, paths, and exception messages. The original document
and query bytes remain available to the tokenizer.

## Error handling, exit codes, and exception safety

Usage, validation, capacity, and file errors are recoverable. Index insertion retains its
transactional contract. A failed file operation never reaches insertion. Success messages
follow the insertion call. If output subsequently fails, the session exits and destroys
its in-memory index without a separate rollback.

Normal EOF, including with stream exception masks enabled, ends the session. Input,
output, or diagnostic stream failures and allocation failures are fatal. Fatal messages
are best effort so failure to write stderr cannot prevent exit. On systems with SIGPIPE,
the executable lets closed-pipe writes surface as stream failures.

| Exit code | Meaning |
| --- | --- |
| 0 | Normal exit, EOF, help, or version. |
| 1 | Fatal runtime, allocation, or stream I/O failure. |
| 2 | Invalid startup arguments. |

## Complexity

Let `B` be bytes in the command line, `F` file bytes, `C` matching candidates, and
`H = min(K, C)`.

| Stage | Cost |
| --- | --- |
| Line reading and parsing | O(B) time. A reusable fixed 1 MiB buffer plus a terminator bounds line storage. A longer line is drained without extra line storage. The parser borrows payloads. |
| File reading | O(F) byte processing and storage plus one fixed chunk, excluding filesystem metadata and device latency. |
| Document indexing | Existing tokenizer and transactional index insertion costs in ADR 0005 and ADR 0006. |
| BM25 candidate generation | Existing query tokenization, term sorting, dictionary lookups, posting traversal, and O(C) candidate memory, as detailed in ADR 0008. |
| Top-K selection | O(C log(H + 1)) for positive H, with O(H) heap and output space. Zero limit skips candidate generation after query validation. |
| Printing and stats | O(H) rows, plus characters written. Stats access is O(1). |

The long-lived session uses index storage proportional to the stored terms, positions, and
document metadata. Transient search memory includes both candidates and selected results.

## Known limitations and future extensions

The index is append-only and in memory. The CLI reads plain text and displays document IDs.
It has no total corpus memory quota. Unicode and Turkish tokenization stay within the
v0.2.0 Query Engine plan. Persistent storage stays within v0.3.0. Advanced CLI features, the
REST API, and the dashboard stay within v0.7.0.

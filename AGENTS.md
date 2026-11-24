# MiniSearch project instructions

- Read the task, the relevant architecture records, the roadmap, and the existing code before editing.
  Stay within the requested milestone. Start each new milestone only with the user's approval.
- Use modern C++20, RAII, target-based CMake, and project headers under `include/minisearch/`.
  Keep the CLI thin and put reusable code in the core library or its public headers.
- Implement the project's data structures and search algorithms directly. Use STL containers as
  comparison baselines in tests and benchmarks. Standard ownership utilities are encouraged.
  Keep dependencies minimal and pin external source versions and GitHub Actions references.
- Maintain the documented preconditions, exception guarantees, iterator invalidation, and time
  and space complexity of public data structures. Verify bounds and allocation failures.
- Write source, comments, identifiers, technical documentation, and commit messages in English.
  Write user-facing explanations and task reports in Turkish.
- Follow `.clang-format` and run the relevant CTest presets. Exercise Debug, Release, and
  supported sanitizer builds for changes to memory handling. Report failed and unrun checks
  explicitly. Publish performance figures only from actual, reproducible measurements.
- Preserve user changes. Push branches or create releases only with explicit user approval.

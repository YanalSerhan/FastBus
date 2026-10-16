## What and why

<!-- One paragraph. Link the TODO item (e.g. M1.5) and any ADR/PRD. -->

## How it was verified

<!-- Commands run, presets, CI links. "Never mark done unless it was actually run." -->

## Checklist

- [ ] Tests added/updated (test commit precedes or accompanies the feature commit)
- [ ] Builds warning-free on MSVC, GCC, Clang; sanitizer jobs green
- [ ] `format-check`, clang-tidy, file-length and layering checks pass
- [ ] Public API has Doxygen comments (what/why, preconditions, thread-safety, errors)
- [ ] Docs updated: CHANGELOG line, known-issues, design-decisions, PRD (if behavior changed)
- [ ] `docs/bug-log.md` updated if a genuine bug was hit
- [ ] Benchmark impact noted (or "n/a")

### Hot-path checklist (publish / loan / take / poll / callback dispatch)

- [ ] No `new`/`malloc`/`std::string`/`std::vector` growth/`std::function` allocation
- [ ] No exceptions (`throw`/`try`)
- [ ] No mutexes, no syscalls, no logging I/O (counters only)
- [ ] Atomics use the weakest correct ordering, with a comment citing the happens-before edge
- [ ] Shared structs: trivially copyable, standard layout, `static_assert`ed size/alignment/offsets, zeroed padding
- [ ] Counting-allocator test still passes

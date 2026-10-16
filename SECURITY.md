# Security policy

fastbus is a research / portfolio project and is **not** hardened for hostile environments. Shared-memory segments are created with owner-only permissions (POSIX `0600`, Windows `Local\` namespace), and all data read from shared memory, UDP or log files is treated as untrusted and validated — but no formal security review has been done.

## Reporting a vulnerability

Please report suspected vulnerabilities privately via GitHub's **"Report a vulnerability"** button (Security → Advisories) on this repository rather than opening a public issue. Expect an acknowledgement within 7 days.

## Supported versions

Only the latest `main` and the most recent tagged release receive fixes.

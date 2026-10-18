# Versions inventory

Every versioned artifact has a version field that is validated on load.

| Artifact | Where | Current |
|---|---|---|
| Library (SemVer) | `project(VERSION)` → generated `fastbus/version.hpp` | 0.1.0 |
| Shm segment layout | `SegmentHeader.layout_version` | 1 (M2) |
| Message header | `MessageHeader.header_version` | 1 (M2) |
| UDP wire protocol | packet header `wire_version` | 1 (M6) |
| Log format | `.fblog` header `format_version` | 1 (M5) |
| IDL language | `fbgen --lang-version` | 1 (M4) |
| Message schema | `@version(N)` per type | 1 (M4) |
| Config files | `"version"` key in each JSON | "1.00" |
| EKF module interface | `modules/ekf` project version | 1.0.0 (M7) |

## Third-party dependencies (pinned in `cmake/Dependencies.cmake`)

| Dependency | Tag | Commit | Commit date | Used for |
|---|---|---|---|---|
| GoogleTest | v1.17.0 | `52eb8108c5bdec04579160ae17225d66034bd723` | 2025-04-30 | tests |
| Google Benchmark | v1.9.4 | `eddb0241389718a23a42db6af5f0164b6e0139af` | 2025-05-19 | benchmarks |
| CLI11 | v2.7.2 | `cbd58a3696887b34c70949aef21a71735a0c2ad5` | 2026-08-02 | tools (M3+) |
| nlohmann_json | v3.12.0 | `55f93686c01528224f448c19128836e7df245f72` | 2025-04-11 | config files (M3+) |
| Eigen | — | — | — | EKF (pinned at M7) |

## Toolchain reference (verified locally for M0)

| Tool | Version |
|---|---|
| CMake | 3.28.3 (minimum supported 3.24) |
| GCC | 13.3.0 |
| Clang | 18.1.3 |
| gcovr | 8.6 (pinned in `uv.lock`) |
| clang-format | 18.1.8 (pinned in `uv.lock`, PyPI wheel — same binary on Windows and Linux) |
| clang-tidy | 19.1.0 on Linux (pinned in `uv.lock`, PyPI wheel); 19.1.5 on Windows (bundled with VS 2022 17.14). Same check set (KI-008). |
| uv | 0.11.x (`pyproject.toml` / `uv.lock`; Python 3.13 via `.python-version`) |
| MSVC | VS 2022 (to be recorded from the first Windows CI run) |

## CI actions (pinned by SHA in `.github/workflows/ci.yml`)

actions/checkout v7.0.1, actions/cache v6.1.0, actions/upload-artifact v7.0.2, astral-sh/setup-uv v10.2.0, hendrikmuhs/ccache-action v1.2.24, lycheeverse/lychee-action v2.9.0.

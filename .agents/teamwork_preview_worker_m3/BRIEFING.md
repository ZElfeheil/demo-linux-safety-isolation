# BRIEFING — 2026-07-31T05:05:00Z

## Mission
Implement Milestone 3: C++20 Userspace Infrastructure & Core Binaries (`common`, `devmem`, `analysis`, and `CMakeLists.txt`).

## 🔒 My Identity
- Archetype: implementer
- Roles: implementer, qa, specialist
- Working directory: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m3
- Original parent: c8b6d41a-8f7c-4d90-93ca-126a79b897ba
- Milestone: Milestone 3 — Userspace Infrastructure & Core Binaries

## 🔒 Key Constraints
- C++20 standard required, warning flags (-Wall -Wextra -Werror -Wpedantic).
- Strict adherence to C++ Core Guidelines (RAII, no raw owning pointers, std::expected, std::span, std::format, std::filesystem).
- Genuine implementation — no hardcoded test results or shortcut facades.
- All CMake targets (library `common`, binaries `devmem` and `analysis`) must compile cleanly.

## Current Parent
- Conversation ID: c8b6d41a-8f7c-4d90-93ca-126a79b897ba
- Updated: 2026-07-31T05:05:00Z

## Task Summary
- **What to build**:
  1. `userspace/CMakeLists.txt`
  2. `userspace/common/scenario.hpp`
  3. `userspace/common/proc_reader.hpp`
  4. `userspace/common/memory_region.hpp`
  5. `userspace/devmem/phys_view.hpp`, `userspace/devmem/phys_view.cpp`, `userspace/devmem/main.cpp`
  6. `userspace/analysis/main.cpp`
- **Success criteria**:
  - `devmem` and `analysis` binaries compiled without errors.
  - All requested features, subcommands, proc parsers, and Markdown generators implemented genuinely.
- **Interface contracts**: `docs/implementation_plan.md` & explorer analysis blueprints.

## Change Tracker
- **Files modified**: NONE yet
- **Build status**: Pending implementation
- **Pending issues**: None

## Quality Status
- **Build/test result**: TBD
- **Lint status**: TBD
- **Tests added/modified**: TBD

## Loaded Skills
- None

## Artifact Index
- `.agents/teamwork_preview_worker_m3/ORIGINAL_REQUEST.md` — Original prompt request
- `.agents/teamwork_preview_worker_m3/BRIEFING.md` — Active briefing file
- `.agents/teamwork_preview_worker_m3/progress.md` — Liveness heartbeat file

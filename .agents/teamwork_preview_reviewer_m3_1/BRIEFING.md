# BRIEFING — 2026-07-31T04:14:25Z

## Mission
Review userspace/CMakeLists.txt and userspace/common/ files for Milestone 3 against task criteria, C++20 standards, C++ Core Guidelines, correctness, and potential failure modes.

## 🔒 My Identity
- Archetype: reviewer_critic
- Roles: reviewer, critic
- Working directory: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_reviewer_m3_1
- Original parent: 7fc57ed2-cafe-4a50-9d02-71478960affa
- Milestone: Milestone 3 - Userspace Infrastructure & Core Binaries
- Instance: 1 of 1

## 🔒 Key Constraints
- Review-only — do NOT modify implementation code in project files
- Must write review report to /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_reviewer_m3_1/handoff.md
- Actively check for integrity violations (hardcoded test results, facade implementations, bypasses)

## Current Parent
- Conversation ID: 7fc57ed2-cafe-4a50-9d02-71478960affa
- Updated: 2026-07-31T04:14:25Z

## Review Scope
- **Files to review**:
  - userspace/CMakeLists.txt
  - userspace/common/scenario.hpp
  - userspace/common/proc_reader.hpp
  - userspace/common/memory_region.hpp
  - userspace/common/expected.hpp
- **Interface contracts**: PROJECT.md / SCOPE.md (if present) or task requirements
- **Review criteria**:
  - C++20 standard configuration (`CMAKE_CXX_STANDARD 20`) and warning flags (`-Wall -Wextra`).
  - `Scenario` concept in `scenario.hpp`.
  - RAII file reading in `proc_reader.hpp` returning `std::expected` / `expected`.
  - `PhysicalMemoryView` in `memory_region.hpp` using `std::unique_ptr<void, MmapDeleter>`, `std::span<const std::byte>`, `std::bit_cast`.
  - C++ Core Guidelines compliance (R.1 RAII, I.11 raw pointers, I.13 array passing, E.1 error handling/exceptions vs expected, ES.49 static_cast/bit_cast over C-style casts).

## Review Checklist
- **Items reviewed**: none yet
- **Verdict**: pending
- **Unverified claims**: none yet

## Attack Surface
- **Hypotheses tested**: TBD
- **Vulnerabilities found**: TBD
- **Untested angles**: TBD

## Key Decisions Made
- Initializing review setup.

## Artifact Index
- ORIGINAL_REQUEST.md — copy of initial request message
- BRIEFING.md — working memory index

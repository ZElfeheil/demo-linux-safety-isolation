## 2026-07-31T07:18:51Z
You are an Explorer agent for Milestone 4 Remediation.
Your Working Directory is /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_remediation

FORENSIC AUDITOR FULL EVIDENCE REPORT (INTEGRITY VIOLATION):
The Forensic Auditor reported INTEGRITY VIOLATION on Milestone 4 with the following findings:

1. Build & Linking Failure (CRITICAL):
   - Target `bin/harness` FAILS to link when compiled with cmake.
   - Undefined symbols for `ScenarioB::setup()`, `run()`, `teardown()`, `ScenarioD::setup()`, `run()`, `teardown()`, and `ScenarioF::setup()`, `run()`, `teardown()`.
   - Root Cause: `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, `scenario_g.cpp` wrapped their member function definitions inside `#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_<X>_HPP` header guards matching their header files, causing the implementation in `.cpp` files to be skipped by the preprocessor!

2. Unvalidated / Always-Pass Test Logic:
   - `userspace/harness/scenarios/scenario_f.cpp` (lines 51-55): Checks `/proc/ctx_monitor_log` for `"FAULT"` or `"BLOCKED"`. If false, line 55 STILL returns `ScenarioStatus::Passed`. The test unconditionally passes even when isolation/fault logging fails.

Task:
Analyze these 2 issues in detail. Formulate an exact fix strategy for:
1. Fixing header guards in `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, `scenario_g.cpp` (removing header guards from `.cpp` files and replacing `#include "scenario_<x>.hpp"` with proper implementation of member methods `Scenario<X>::setup()`, `run()`, `teardown()`).
2. Fixing `scenario_f.cpp` validation logic so it genuinely checks for SMMU/CTX fault logging and returns `ScenarioStatus::Failed` if the expected protection logs are absent.

Save your detailed fix blueprint to /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_remediation/analysis.md and send your report to parent.

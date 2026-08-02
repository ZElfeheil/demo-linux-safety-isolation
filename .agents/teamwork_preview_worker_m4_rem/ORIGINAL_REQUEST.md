## 2026-07-31T07:22:07Z
You are the Worker agent for Milestone 4 Remediation.
Your Working Directory is /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m4_rem

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Task:
Execute the full set of Milestone 4 remediation fixes according to the Remediation Explorer blueprint in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_remediation/analysis.md:

1. Fix Scenario Header Guards & Out-of-Line Method Definitions:
   - In userspace/harness/scenarios/scenario_b.cpp, scenario_d.cpp, scenario_f.cpp, scenario_g.cpp:
     - Remove header guards (#ifndef USERSPACE_HARNESS_SCENARIOS_...) from the .cpp files.
     - Include header file #include "scenario_<x>.hpp".
     - Define methods out-of-line: Scenario<X>::setup(), Scenario<X>::run(), Scenario<X>::teardown().

2. Fix Unconditional Pass Bug in scenario_f.cpp:
   - In ScenarioF::run(), check both /proc/ctx_monitor_log and /proc/smmu_guard_log.
   - If either log contains "FAULT" or "BLOCKED", return ScenarioStatus::Passed.
   - If neither log records a fault/blocked entry, return ScenarioStatus::Failed with message "No CTX or SMMU fault/blocked log recorded during isolation test".

3. Fix ModuleLoader & Harness Async-Signal Safety and CLI Propagation:
   - Make ModuleLoader signal handling async-signal-safe (set atomic flag, avoid unsafe std::mutex or std::system in signal handler).
   - Ensure --start-at <id> CLI flag is properly passed when spawning tmux pane.

Requirements:
- Verify file compilation / syntax.
- Save handoff report to /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m4_rem/handoff.md and report to parent.

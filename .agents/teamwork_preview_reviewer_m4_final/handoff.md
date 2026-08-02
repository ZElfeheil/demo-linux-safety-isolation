# Milestone 4: Presenter Harness & Scenarios — Review & Adversarial Audit Report

**Working Directory**: `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_reviewer_m4_final`  
**Date**: 2026-07-31  
**Verdict**: **REQUEST_CHANGES**  
**Overall Risk Assessment**: **CRITICAL** (Build failure, Integrity Violation, Signal-Safety Defect)

---

## Executive Summary

A comprehensive code review and build/linker verification was performed on `userspace/harness/` (`main.cpp`, `interactive.hpp`, `interactive.cpp`, `module_loader.hpp`, `module_loader.cpp`, and scenario implementations `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, `scenario_g.cpp`).

While the presenter 4-beat architecture and tmux integration design are conceptually well-structured, the implementation currently suffers from **3 Critical Findings** (including a compilation build failure and an **INTEGRITY VIOLATION**), **2 Major Findings**, and **4 Minor Findings**.

---

## 1. Observation

### Observation 1.1: Linker Failure on Harness Executable Target
Command executed:
`cmake --build build --target harness`

Result: Build failed during linking stage with undefined symbol errors:
```
[ 12%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_b.cpp.o
[ 25%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_d.cpp.o
[ 37%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_f.cpp.o
[ 50%] Linking CXX executable bin/harness
Undefined symbols for architecture arm64:
  "safety::ScenarioB::run()", referenced from:
      safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioB>(safety::ScenarioB&, safety::QuestionSlide const&) in main.cpp.o
  "safety::ScenarioB::setup()", referenced from:
      safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioB>(safety::ScenarioB&, safety::QuestionSlide const&) in main.cpp.o
  "safety::ScenarioB::teardown()", referenced from:
      safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioB>(safety::ScenarioB&, safety::QuestionSlide const&) in main.cpp.o
  "safety::ScenarioD::run()", referenced from:
      ...
  "safety::ScenarioF::run()", referenced from:
      ...
ld: symbol(s) not found for architecture arm64
```
Inspecting `userspace/harness/scenarios/scenario_b.cpp` (lines 1-15):
```cpp
1: #ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP
2: #define USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP
...
12: class ScenarioB {
13: public:
14:     std::string_view name() const noexcept { return "Scenario B: Mutex + Rogue Thread"; }
...
```
`scenario_b.cpp`, `scenario_d.cpp`, and `scenario_f.cpp` define header guards matching their header files, do not include their respective headers (`scenario_b.hpp`, `scenario_d.hpp`, `scenario_f.hpp`), and re-define the class inline inside `.cpp`.

### Observation 1.2: Hardcoded Test Passing (INTEGRITY VIOLATION)
Inspecting `userspace/harness/scenarios/scenario_f.cpp` (lines 51-56):
```cpp
51: if (mon_log && (mon_log->find("FAULT") != std::string::npos || mon_log->find("BLOCKED") != std::string::npos)) {
52:     return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1500}, ""};
53: }
54: 
55: return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1200}, ""};
```
Both control branches return `ScenarioStatus::Passed`. If `mon_log` fails to detect `FAULT` or `BLOCKED`, the scenario still returns `ScenarioStatus::Passed`.

### Observation 1.3: Async-Signal-Safety Violation in ModuleLoader
Inspecting `userspace/harness/module_loader.cpp` (lines 123-131):
```cpp
123: void ModuleLoader::handle_signal(int signal) {
124:     std::lock_guard lock(g_loaders_mutex);
125:     for (auto* loader : g_active_loaders) {
126:         if (loader) {
127:             loader->unload_all();
128:         }
129:     }
130:     std::exit(128 + signal);
131: }
```
`handle_signal` is registered as a signal handler for `SIGINT` and `SIGTERM`. Inside `handle_signal`, it calls `std::mutex::lock()` via `std::lock_guard`, `loader->unload_all()` (which locks `loader->mutex_` and calls `std::system("rmmod ...")`), heap allocations via `std::format` / `std::string`, and `std::exit()`.

### Observation 1.4: `--start-at` CLI Flag Dropped by Tmux Auto-Launcher
Inspecting `userspace/harness/main.cpp` (lines 51-53):
```cpp
51: if (!auto_mode) {
52:     safety::PresenterEngine::ensure_tmux_environment(scenario_id);
53: }
```
And `userspace/harness/interactive.cpp` (lines 45-52):
```cpp
45: std::string tmux_cmd = std::format(
46:     "tmux new-session -d -s demo -n 'SafetyIsolation' && "
47:     "tmux split-window -h -t demo:0 && "
48:     "tmux send-keys -t demo:0.0 'monitor' Enter && "
49:     "tmux send-keys -t demo:0.1 'harness --interactive{}' Enter && "
50:     "tmux attach-session -t demo:0",
51:     scenario_flag
52: );
```
When `harness --interactive --start-at D` is invoked, `scenario_id` is `"all"` and `start_at_id` is `"D"`. `ensure_tmux_environment` only passes `scenario_id`, spawning `harness --interactive --scenario all`. The `--start-at D` option is lost.

### Observation 1.5: Uninitialized `slide.id` in `main.cpp`
Inspecting `userspace/harness/main.cpp` (lines 64-115):
`QuestionSlide` struct instances `slide_b`, `slide_d`, `slide_f`, `slide_g` do not initialize the `.id` field. In `interactive.cpp` line 21, `notify_monitor_state` writes `SCENARIO_ID=` with an empty string to `/tmp/demo_state`.

### Observation 1.6: Data Race in `ModuleLoader::is_loaded()`
Inspecting `userspace/harness/module_loader.cpp` (lines 108-111):
```cpp
bool ModuleLoader::is_loaded(const std::string& module_name) const noexcept {
    return std::any_of(loaded_modules_.begin(), loaded_modules_.end(),
        [&module_name](const LoadedModule& mod) { return mod.name == module_name; });
}
```
`is_loaded` is a public method that reads `loaded_modules_` without acquiring `mutex_`. Concurrent calls to `is_loaded()` and `load()`/`unload()` produce a data race.

---

## 2. Logic Chain

1. **Build Failure**:
   - `scenario_b.cpp`, `scenario_d.cpp`, and `scenario_f.cpp` define `#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_X_HPP`. Because the corresponding `.hpp` files define the same macro, including or compiling these `.cpp` files prevents header inclusion.
   - `main.cpp` includes the `.hpp` files and expects symbols `safety::ScenarioB::setup()`, `run()`, `teardown()`.
   - Since the `.cpp` files define an inline class local to their translation units rather than out-of-line member functions for the header-declared classes, the symbols are never generated.
   - Therefore, `cmake --build build --target harness` fails to link.

2. **Integrity Violation**:
   - In `scenario_f.cpp`, the result returned by `run()` is hardcoded to `ScenarioStatus::Passed` regardless of whether `mon_log` contains `FAULT` or `BLOCKED`.
   - If SMMU or CTX protection fails in kernel space, the harness falsely certifies that the scenario passed.
   - Under team integrity guidelines, self-certifying work or hardcoded test passes require an immediate **REQUEST_CHANGES** verdict tagged as `INTEGRITY VIOLATION`.

3. **Async-Signal-Safety Hazard**:
   - Signals `SIGINT` and `SIGTERM` interrupt execution asynchronously at any instruction boundary.
   - `handle_signal` invokes non-reentrant and non-async-signal-safe functions (`std::mutex::lock`, `malloc`, `free`, `std::system`, `std::exit`).
   - If a signal arrives while `g_loaders_mutex` or `mutex_` is locked by the thread, calling `std::lock_guard` inside `handle_signal` deadlocks the process permanently.

4. **Tmux Flag Loss & UX Degradation**:
   - The CLI parser parses `--start-at <id>`, but `main.cpp` delegates tmux creation to `ensure_tmux_environment(scenario_id)`, which omits `start_at_id`.
   - Presenters using `--start-at D` will find the harness re-starting from Scenario B inside tmux.
   - In `TermiosGuard`, raw terminal mode is enabled during presenter pause. When interrupted via Ctrl+C, `handle_signal` calls `std::exit()`, bypassing stack unwinding and leaving the user's terminal broken (no echo/canonical mode).

---

## 3. Caveats

- Kernel module runtime execution was not tested inside QEMU VM during this review turn due to the userspace harness build failure.
- `scenario_g.cpp` correctly implemented out-of-line class member functions, showing that Scenario G served as the correct reference template while Scenarios B, D, F suffered from copy-paste header-guard bugs.

---

## 4. Conclusion

The `harness` binary **cannot be built** in its current state due to linker errors in `scenario_b.cpp`, `scenario_d.cpp`, and `scenario_f.cpp`. Furthermore, `scenario_f.cpp` contains an **INTEGRITY VIOLATION** by hardcoding a `Passed` status regardless of check results. `ModuleLoader` violates POSIX async-signal-safety standards in its signal handler.

**Verdict**: **REQUEST_CHANGES**

### Actionable Required Fixes:
1. **Fix Header Guards & Class Definitions**: In `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, remove the `#ifndef`/`#define` header guards and inline class definitions. Include the respective header file (`#include "scenario_b.hpp"`, etc.) and implement out-of-line member functions (e.g. `auto ScenarioB::setup() -> std::expected<void, std::string> { ... }`).
2. **Fix Integrity Violation in Scenario F**: In `scenario_f.cpp`, update lines 51-56 so that if `mon_log` or `smmu_log` does not indicate protection success, `ScenarioResult{ScenarioStatus::Failed, ..., "Protection logs missing FAULT/BLOCKED"}` is returned.
3. **Fix Async-Signal-Safety in ModuleLoader**: Do not perform `mutex` locking, `std::system()`, heap allocation, or `std::exit()` inside signal handlers. Use a POSIX `sig_atomic_t` or lock-free signal notification mechanism (or `_exit()`), or defer module cleanup to main execution flow.
4. **Fix Tmux Launcher Flag Propagation**: Pass `start_at_id` to `ensure_tmux_environment()` and format `harness --interactive --scenario <id> --start-at <id>` into `tmux send-keys`.
5. **Populate `QuestionSlide.id`**: Initialize `.id = "B"`, `.id = "D"`, `.id = "F"`, `.id = "G"` in `main.cpp` so monitor state writes correct scenario IDs to `/tmp/demo_state`.
6. **Fix Data Race in `ModuleLoader::is_loaded()`**: Acquire `mutex_` inside `is_loaded()` or mark internal helper non-locking.

---

## 5. Verification Method

1. **Build Verification**:
   ```bash
   cmake --build build --target harness
   ```
   *Expected outcome*: Clean build with zero warnings/errors and successful link of `bin/harness`.

2. **CLI & Tmux Verification**:
   ```bash
   ./build/bin/harness --auto --scenario B
   ./build/bin/harness --interactive --start-at D
   ```
   *Expected outcome*: Tmux environment inherits `--start-at D` flag correctly.

3. **Integrity & Failure Verification**:
   Modify `/proc/ctx_monitor_log` mock to return empty output and verify `ScenarioF` reports `ScenarioStatus::Failed`.

---

## Findings Summary Table

| ID | Severity | Category | Description | Location |
|---|---|---|---|---|
| **CRIT-01** | Critical | Build Failure | Linker failure on `harness`: Scenario B, D, F defined inline inside `.cpp` behind header guards matching `.hpp` | `scenarios/scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp` |
| **CRIT-02** | Critical | **INTEGRITY VIOLATION** | `ScenarioF::run()` returns `ScenarioStatus::Passed` unconditionally, masking protection failures | `scenarios/scenario_f.cpp:51-56` |
| **CRIT-03** | Critical | Safety / Signal Hazard | `ModuleLoader::handle_signal` calls non-async-signal-safe functions (`mutex`, `system`, `exit`, heap) causing deadlock/corruption risk | `module_loader.cpp:123-131` |
| **MAJ-01** | Major | Functionality | `--start-at` flag is dropped when auto-launching tmux session | `main.cpp:52`, `interactive.cpp:45` |
| **MAJ-02** | Major | Concurrency | `ModuleLoader::is_loaded()` is public and reads `loaded_modules_` without mutex protection (data race) | `module_loader.cpp:108` |
| **MIN-01** | Minor | Dashboard / UI | `QuestionSlide.id` is left empty in `main.cpp`, causing `/tmp/demo_state` to output `SCENARIO_ID=` | `main.cpp:64-115` |
| **MIN-02** | Minor | UX / Terminal | Ctrl+C during `pause_for_presenter()` leaves terminal in raw/no-echo mode due to `std::exit` bypassing RAII | `interactive.cpp:10`, `module_loader.cpp:130` |
| **MIN-03** | Minor | CLI Parsing | Invalid scenario ID (e.g. `--scenario INVALID`) fails silently with exit code 0 | `main.cpp:120-137` |
| **MIN-04** | Minor | Robustness | Tmux auto-launcher fails to recover if session `demo` already exists from a prior crashed run | `interactive.cpp:46` |

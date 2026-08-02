# Forensic Audit Report & Handoff Report

**Work Product**: `userspace/monitor/` and `userspace/harness/`
**Profile**: General Project
**Verdict**: INTEGRITY VIOLATION

---

## Forensic Audit Report

### Phase Results
- **Build and execution**: **FAIL** — `harness` target fails to link due to missing symbol definitions for `ScenarioB`, `ScenarioD`, and `ScenarioF`.
- **Prohibited patterns check**: **FAIL** — `ScenarioF::run()` contains unvalidated always-pass test logic returning `Passed` even when kernel logs contain no fault/blocked indication.
- **Monitor proc/trace polling**: **PASS** — `monitor` genuinely polls `/proc/safety_mem_status` (via `ProcReader`) and `/sys/kernel/tracing/trace_pipe` on 100ms loops.
- **ModuleLoader insmod/rmmod**: **PASS** — `ModuleLoader` genuinely executes system `insmod` and `rmmod` commands via `std::system`.
- **Trigger writes**: **PASS** — Scenarios D and F write genuine trigger codes (`3` and `1`) to `/proc/bad_driver_ts`.

---

## 1. Observation

### Observation 1: Build Linker Failure for `harness` Executable
- **Command executed**: `cmake -B build && cmake --build build` in `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace`
- **Output / Error**:
  ```text
  [100%] Linking CXX executable bin/harness
  Undefined symbols for architecture arm64:
    "safety::ScenarioB::run()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioB>(safety::ScenarioB&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioB::setup()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioB>(safety::ScenarioB&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioB::teardown()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioB>(safety::ScenarioB&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioD::run()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioD>(safety::ScenarioD&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioD::setup()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioD>(safety::ScenarioD&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioD::teardown()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioD>(safety::ScenarioD&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioF::run()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioF>(safety::ScenarioF&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioF::setup()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioF>(safety::ScenarioF&, safety::QuestionSlide const&) in main.cpp.o
    "safety::ScenarioF::teardown()", referenced from:
        safety::ScenarioResult safety::PresenterEngine::run_scenario<safety::ScenarioF>(safety::ScenarioF&, safety::QuestionSlide const&) in main.cpp.o
  ld: symbol(s) not found for architecture arm64
  clang++: error: linker command failed with exit code 1 (use -v to see invocation)
  make[2]: *** [bin/harness] Error 1
  ```

### Observation 2: Incorrect Header Guard / Class Redefinition Flaw in Scenario Source Files
- **File**: `userspace/harness/scenarios/scenario_b.cpp` (lines 1-2, 12-50)
  ```cpp
  1: #ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP
  2: #define USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP
  ...
  12: class ScenarioB {
  ...
  16:     std::expected<void, std::string> setup() { ... }
  22:     ScenarioResult run() { ... }
  44:     void teardown() { ... }
  ```
- **File**: `userspace/harness/scenarios/scenario_d.cpp` (lines 1-2, 13-62)
  ```cpp
  1: #ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP
  2: #define USERSPACE_HARNESS_SCENARIOS_SCENARIO_D_HPP
  ...
  13: class ScenarioD {
  ...
  ```
- **File**: `userspace/harness/scenarios/scenario_f.cpp` (lines 1-2, 13-64)
  ```cpp
  1: #ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP
  2: #define USERSPACE_HARNESS_SCENARIOS_SCENARIO_F_HPP
  ...
  13: class ScenarioF {
  ...
  ```
- **File**: `userspace/harness/scenarios/scenario_b.hpp` (lines 11-23)
  ```cpp
  class ScenarioB {
  public:
      [[nodiscard]] std::string_view name() const noexcept {
          return "Scenario B: Mutex + Rogue Thread";
      }

      auto setup() -> std::expected<void, std::string>;
      auto run() -> ScenarioResult;
      void teardown();
  ...
  };
  ```

### Observation 3: Unvalidated Always-Pass Logic Flaw in Scenario F
- **File**: `userspace/harness/scenarios/scenario_f.cpp` (lines 51-55)
  ```cpp
  51: if (mon_log && (mon_log->find("FAULT") != std::string::npos || mon_log->find("BLOCKED") != std::string::npos)) {
  52:     return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1500}, ""};
  53: }
  54:
  55: return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1200}, ""};
  ```

### Observation 4: Genuine Monitor Polling Code
- **File**: `userspace/monitor/main.cpp` (lines 65-105, 157-180)
  ```cpp
  65: safety::ProcReader proc_reader("/proc/safety_mem_status");
  ...
  157: std::ifstream trace_file("/sys/kernel/tracing/trace_pipe");
  158: if (!trace_file.is_open()) {
  159:     trace_file.open("/sys/kernel/debug/tracing/trace_pipe");
  160: }
  ```

### Observation 5: Genuine ModuleLoader Execution & Trigger Writes
- **File**: `userspace/harness/module_loader.cpp` (lines 68, 89, 103)
  ```cpp
  68: int ret = std::system(cmd.c_str()); // cmd = "insmod <path> [params]"
  89: int ret = std::system(cmd.c_str()); // cmd = "rmmod <name>"
  ```
- **File**: `userspace/harness/scenarios/scenario_d.cpp` (lines 33-37)
  ```cpp
  33: std::ofstream bad_driver("/proc/bad_driver_ts");
  34: if (bad_driver.is_open()) {
  35:     bad_driver << "3";
  36:     bad_driver.flush();
  37: }
  ```

---

## 2. Logic Chain

1. **Build Failure Logic**:
   - `userspace/harness/main.cpp` includes `scenarios/scenario_b.hpp`, `scenarios/scenario_d.hpp`, and `scenarios/scenario_f.hpp`.
   - These header files declare non-inline member functions: `setup()`, `run()`, and `teardown()`, and define macro guards (e.g. `USERSPACE_HARNESS_SCENARIOS_SCENARIO_B_HPP`).
   - The `.cpp` files (`scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`) check `#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_<X>_HPP`. In their own translation units, because the header guard macro was not set before reading the `.cpp` file, they define `class Scenario<X>` with inline methods within the `.cpp` file rather than implementing `Scenario<X>::setup()`, `Scenario<X>::run()`, and `Scenario<X>::teardown()`.
   - When the compiler links `main.cpp.o` and the scenario object files, the non-inline symbols declared in `scenario_<x>.hpp` are missing. The build fails during linking.
   - Per Forensic Integrity Audit guidelines Phase 2 Step 4 ("Build and run"), any work product that fails to build is an immediate integrity failure.

2. **Self-Certifying / Always-Pass Test Logic**:
   - In `scenario_f.cpp`, lines 51-55: line 51 checks if `/proc/ctx_monitor_log` contains `"FAULT"` or `"BLOCKED"`. If false, control falls through to line 55 which returns `ScenarioStatus::Passed`.
   - This means `ScenarioF` returns `Passed` even if no security isolation fault was logged or triggered.
   - This violates the Integrity Forensics requirement prohibiting self-certifying / dummy pass returns without validation.

3. **Monitor and ModuleLoader Implementation Verification**:
   - `monitor` genuinely reads `/proc/safety_mem_status` and `/sys/kernel/tracing/trace_pipe`.
   - `ModuleLoader` genuinely executes system `insmod` and `rmmod` via `std::system`.
   - `ScenarioD` and `ScenarioF` write to `/proc/bad_driver_ts`.
   - Despite authentic underlying system interfaces, the broken build and always-pass logic in Scenario F invalidate the work product.

---

## 3. Caveats

- **QEMU Kernel Module Execution**: Direct kernel execution of `insmod` and `/proc` writes was verified statically and via build system checks. Full runtime QEMU execution was not performed because `harness` failed to compile.
- **Scenario G**: `ScenarioG` in `scenario_g.cpp` was correctly written (including `scenario_g.hpp` and defining `ScenarioG::setup()`, `run()`, `teardown()`). However, `harness` as a whole fails to link due to Scenarios B, D, and F.

---

## 4. Conclusion

The work product under `userspace/monitor/` and `userspace/harness/` receives a strict verdict of **INTEGRITY VIOLATION**.

While `monitor` and `ModuleLoader` contain authentic system polling and execution logic:
1. The `harness` binary fails to build/link due to macro guard and class definition errors in `scenario_b.cpp`, `scenario_d.cpp`, and `scenario_f.cpp`.
2. `ScenarioF` contains a test logic defect that unconditionally returns `ScenarioStatus::Passed` even when kernel logs indicate no fault or blockage.

---

## 5. Verification Method

To independently verify these findings:

1. **Verify Linker Error**:
   ```bash
   cd /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace
   cmake -B build
   cmake --build build
   ```
   *Expected result*: Build fails at 100% linking `bin/harness` with `Undefined symbols for architecture arm64`.

2. **Inspect Broken Header Guards in Scenario C++ Files**:
   ```bash
   head -n 5 /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_b.cpp
   head -n 5 /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_d.cpp
   head -n 5 /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_f.cpp
   ```
   *Expected result*: All three `.cpp` files begin with `#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_<X>_HPP`.

3. **Inspect Always-Pass Logic in Scenario F**:
   ```bash
   sed -n '50,56p' /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_f.cpp
   ```
   *Expected result*: Line 55 returns `ScenarioStatus::Passed` unconditionally when `mon_log` check fails.

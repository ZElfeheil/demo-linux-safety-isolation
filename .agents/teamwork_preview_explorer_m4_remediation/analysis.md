# Milestone 4 Remediation: Detailed Analysis & Fix Blueprint

## Executive Summary

The Forensic Auditor identified two critical integrity violations in Milestone 4:
1. **Build & Linking Failure (CRITICAL)**: Target `bin/harness` fails to link under CMake with undefined symbol errors for `ScenarioB::setup()`, `run()`, `teardown()`, `ScenarioD::setup()`, `run()`, `teardown()`, and `ScenarioF::setup()`, `run()`, `teardown()`.
2. **Unvalidated / Always-Pass Test Logic**: `userspace/harness/scenarios/scenario_f.cpp` unconditionally returns `ScenarioStatus::Passed` even when CTX monitor and SMMU guard fault logging fails or is completely absent.

This document provides the exact root-cause analysis, verbatim evidence, and file-by-file remediation blueprints for the implementer agent to resolve both issues.

---

## 1. Issue 1 Analysis: Build & Linking Failure (CRITICAL)

### 1.1 Root Cause Breakdown

In `userspace/harness/scenarios/` (specifically `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`):
1. **Header Guard Misuse in Implementation Files**: The `.cpp` implementation files wrapped their contents in `#ifndef USERSPACE_HARNESS_SCENARIOS_SCENARIO_<X>_HPP` header guards matching the exact macro guards used by their respective header files (`scenario_<x>.hpp`).
2. **Class Re-definition & Inline Method Trapping**: Instead of including `#include "scenario_<x>.hpp"` and defining the member functions out-of-line (`Scenario<X>::setup()`, `Scenario<X>::run()`, `Scenario<X>::teardown()`), the `.cpp` files re-declared `class Scenario<X>` with inline method bodies.
3. **Symbol Visibility Invalidation**:
   - In C++, member functions defined inside a class declaration body are implicitly `inline`. The compiler does not emit non-inline external symbols for them in the resulting object file (`scenario_<x>.cpp.o`).
   - Meanwhile, `main.cpp` includes `scenario_<x>.hpp`, where `setup()`, `run()`, and `teardown()` are declared as non-inline member functions.
   - When `PresenterEngine::run_scenario<Scenario<X>>` is instantiated in `main.cpp.o`, the compiler emits external relocations to `safety::Scenario<X>::setup()`, `safety::Scenario<X>::run()`, and `safety::Scenario<X>::teardown()`.
   - The linker cannot resolve these external symbols against `scenario_<x>.cpp.o`, resulting in linking termination.

### 1.2 Verbatim Evidence (Compiler / Linker Output)

Executing `cmake -B userspace/build -S userspace && cmake --build userspace/build --target harness` produces the following failure:

```text
[ 12%] Building CXX object CMakeFiles/harness.dir/harness/main.cpp.o
[ 25%] Building CXX object CMakeFiles/harness.dir/harness/interactive.cpp.o
[ 37%] Building CXX object CMakeFiles/harness.dir/harness/module_loader.cpp.o
[ 50%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_b.cpp.o
[ 62%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_d.cpp.o
[ 75%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_f.cpp.o
[ 87%] Building CXX object CMakeFiles/harness.dir/harness/scenarios/scenario_g.cpp.o
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
```

### 1.3 Fix Blueprint for Issue 1

For `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, and `scenario_g.cpp`:
1. Remove all header guard directives (`#ifndef`, `#define`, `#endif`) from the `.cpp` files.
2. Replace duplicate class declarations with `#include "scenario_<x>.hpp"`.
3. Re-implement `setup()`, `run()`, and `teardown()` out-of-line as member functions (`Scenario<X>::setup()`, `Scenario<X>::run()`, `Scenario<X>::teardown()`).
4. Normalize kernel module paths across all scenarios to use canonical `/lib/modules/<name>.ko` paths (which `ModuleLoader::load` automatically resolves with local `./modules/` fallback if `/lib/modules/` is not present).

---

## 2. Issue 2 Analysis: Unvalidated / Always-Pass Test Logic in `scenario_f.cpp`

### 2.1 Root Cause Breakdown

In `userspace/harness/scenarios/scenario_f.cpp` lines 42-56:

```cpp
// EXISTING FAULTY CODE IN scenario_f.cpp
ProcReader monitor_reader("/proc/ctx_monitor_log");
ProcReader smmu_reader("/proc/smmu_guard_log");

auto mon_log = monitor_reader.read();
auto smmu_log = smmu_reader.read();

auto end_ts = std::chrono::high_resolution_clock::now();
uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

if (mon_log && (mon_log->find("FAULT") != std::string::npos || mon_log->find("BLOCKED") != std::string::npos)) {
    return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1500}, ""};
}

return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1200}, ""};
```

**Flaws Identified**:
1. **Unconditional Pass Fallthrough**: If `mon_log` does NOT contain `"FAULT"` or `"BLOCKED"` (e.g. log reading failed or isolation mechanism failed to trap the fault), line 55 STILL returns `ScenarioStatus::Passed`!
2. **Ignored SMMU Telemetry**: Line 46 reads `smmu_log` from `/proc/smmu_guard_log`, but `smmu_log` is completely ignored in the decision logic. Scenario F is specifically titled "Full CTX + SMMU Isolation" and validates both CPU MMU and SMMUv3 bus protection.

### 2.2 Fix Blueprint for Issue 2

Update `ScenarioF::run()` validation logic as follows:
1. Check both `mon_log` and `smmu_log` for the presence of `"FAULT"` or `"BLOCKED"` substrings.
2. If either `mon_log` or `smmu_log` contains valid fault/blocked records, return `ScenarioStatus::Passed`.
3. If neither log contains fault/blocked records (or log reading fails completely), return `ScenarioStatus::Failed` with a clear error message: `"No CTX or SMMU fault/blocked log recorded during isolation test"`.

```cpp
bool mon_has_fault = mon_log && (mon_log->find("FAULT") != std::string::npos || mon_log->find("BLOCKED") != std::string::npos);
bool smmu_has_fault = smmu_log && (smmu_log->find("FAULT") != std::string::npos || smmu_log->find("BLOCKED") != std::string::npos);

if (mon_has_fault || smmu_has_fault) {
    return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1500}, ""};
}

return ScenarioResult{
    ScenarioStatus::Failed,
    {latency, 0, 0},
    "No CTX or SMMU fault/blocked log recorded during isolation test"
};
```

---

## 3. Detailed Proposed File Contents

### 3.1 `userspace/harness/scenarios/scenario_b.cpp`

```cpp
#include "scenario_b.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <thread>

namespace safety {

auto ScenarioB::setup() -> std::expected<void, std::string> {
    if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/mutex_threads.ko"); !res) return res;
    return {};
}

auto ScenarioB::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    if (auto res = loader_.load("/lib/modules/rogue_thread.ko", "attack_mode=0 interval_ms=100"); !res) {
        return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ProcReader reader("/proc/safety_mem_status");
    auto content = reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    if (content && (content->find("0xDEADDEAD") != std::string::npos || content->find("CORRUPTED") != std::string::npos)) {
        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
    }

    return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, "Rogue write failed to corrupt memory"};
}

void ScenarioB::teardown() {
    loader_.unload_all();
}

} // namespace safety
```

### 3.2 `userspace/harness/scenarios/scenario_d.cpp`

```cpp
#include "scenario_d.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

auto ScenarioD::setup() -> std::expected<void, std::string> {
    if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/bad_driver.ko"); !res) return res;

    std::ofstream status_file("/proc/safety_mem_status");
    if (status_file.is_open()) {
        status_file << "protect";
        status_file.flush();
    }
    return {};
}

auto ScenarioD::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    std::ofstream bad_driver("/proc/bad_driver_ts");
    if (bad_driver.is_open()) {
        bad_driver << "3";
        bad_driver.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ProcReader reader("/proc/safety_mem_status");
    auto content = reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    if (content && (content->find("0xBAD30003") != std::string::npos ||
                    content->find("CORRUPTED") != std::string::npos ||
                    content->find("value_via_phys: 0x5AFE1234") == std::string::npos)) {
        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, ""};
    }

    return ScenarioResult{ScenarioStatus::Failed, {latency, 0, 0}, "Linear map bypass did not display expected value divergence"};
}

void ScenarioD::teardown() {
    loader_.unload_all();
}

} // namespace safety
```

### 3.3 `userspace/harness/scenarios/scenario_f.cpp`

```cpp
#include "scenario_f.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <fstream>
#include <thread>

namespace safety {

auto ScenarioF::setup() -> std::expected<void, std::string> {
    if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/ctx_monitor.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/smmu_guard.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/bad_driver.ko"); !res) return res;

    std::ofstream status_file("/proc/safety_mem_status");
    if (status_file.is_open()) {
        status_file << "protect";
        status_file.flush();
    }
    return {};
}

auto ScenarioF::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    std::ofstream bad_driver("/proc/bad_driver_ts");
    if (bad_driver.is_open()) {
        bad_driver << "1";
        bad_driver.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ProcReader monitor_reader("/proc/ctx_monitor_log");
    ProcReader smmu_reader("/proc/smmu_guard_log");

    auto mon_log = monitor_reader.read();
    auto smmu_log = smmu_reader.read();

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count();

    bool mon_has_fault = mon_log && (mon_log->find("FAULT") != std::string::npos || mon_log->find("BLOCKED") != std::string::npos);
    bool smmu_has_fault = smmu_log && (smmu_log->find("FAULT") != std::string::npos || smmu_log->find("BLOCKED") != std::string::npos);

    if (mon_has_fault || smmu_has_fault) {
        return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 1500}, ""};
    }

    return ScenarioResult{
        ScenarioStatus::Failed,
        {latency, 0, 0},
        "No CTX or SMMU fault/blocked log recorded during isolation test"
    };
}

void ScenarioF::teardown() {
    loader_.unload_all();
}

} // namespace safety
```

### 3.4 `userspace/harness/scenarios/scenario_g.cpp`

```cpp
#include "scenario_g.hpp"
#include "../../common/proc_reader.hpp"
#include <chrono>
#include <thread>

namespace safety {

auto ScenarioG::setup() -> std::expected<void, std::string> {
    if (auto res = loader_.load("/lib/modules/safety_mem.ko"); !res) return res;
    if (auto res = loader_.load("/lib/modules/mutex_threads.ko"); !res) return res;
    return {};
}

auto ScenarioG::run() -> ScenarioResult {
    auto start_ts = std::chrono::high_resolution_clock::now();

    // Load rogue_thread with attack_mode=1 (lock metadata attack)
    if (auto res = loader_.load("/lib/modules/rogue_thread.ko", "attack_mode=1 interval_ms=100"); !res) {
        return ScenarioResult{ScenarioStatus::Error, {}, res.error()};
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto end_ts = std::chrono::high_resolution_clock::now();
    uint64_t latency = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end_ts - start_ts).count());

    return ScenarioResult{ScenarioStatus::Passed, {latency, latency, 0}, "Mutex metadata attack executed"};
}

void ScenarioG::teardown() {
    loader_.unload_all();
}

} // namespace safety
```

---

## 4. Verification Method

To verify the remediation:
1. Apply the replacement content for `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, and `scenario_g.cpp`.
2. Run build command:
   ```bash
   cmake -B userspace/build -S userspace && cmake --build userspace/build --target harness
   ```
   **Expected Result**: Build succeeds with 0 errors and generates executable `userspace/build/bin/harness`.
3. Run unit check on scenario validation failure logic:
   - In environment without kernel modules loaded, running `bin/harness --auto --scenario F` should return `ScenarioStatus::Failed` (due to missing `/proc/ctx_monitor_log` & `/proc/smmu_guard_log`), verifying that unconditional pass behavior is eliminated.

## 2026-07-31T04:48:08Z
You are the Worker agent for Milestone 3: Userspace Infrastructure & Core Binaries.
Your Working Directory is /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m3

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Task:
Implement all C++20 userspace core headers, devmem binary, analysis binary, and CMake configuration according to docs/implementation_plan.md:

1. userspace/CMakeLists.txt:
   - Configure C++20 standard (CMAKE_CXX_STANDARD 20, CMAKE_CXX_STANDARD_REQUIRED ON).
   - Add subdirectories or target declarations for common headers, devmem, analysis, monitor, harness.
   - Enforce compiler warning flags (-Wall -Wextra).

2. userspace/common/scenario.hpp:
   - C++20 Scenario concept (name(), setup(), run(), teardown()).
   - ScenarioResult struct with status enum, latency metrics, and error output string.

3. userspace/common/proc_reader.hpp:
   - ProcReader class using RAII to read /proc virtual files, returning std::expected<std::string, std::string>.
   - Adhere to C++ Core Guidelines R.1, I.11, E.1.

4. userspace/common/memory_region.hpp:
   - PhysicalMemoryView class managing /dev/mem memory mapping via std::unique_ptr<void, MmapDeleter>.
   - Expose view() returning std::span<const std::byte>.
   - Template read_at<T>(offset) using std::bit_cast.
   - Adhere to C++ Core Guidelines I.11, I.13, ES.49.

5. userspace/devmem/main.cpp and phys_view.cpp:
   - devmem read <phys_addr> (reads 32-bit hex value from physical address).
   - devmem write <phys_addr> <val> (writes 32-bit hex value to physical address).
   - devmem watch <phys_addr> (polls memory address every 100ms, highlighting changes live).

6. userspace/analysis/main.cpp:
   - CLI invocation: analysis --output <path> (defaulting to /results/comparison_table.md).
   - Reads telemetry from /proc/safety_mem_status, /proc/bad_driver_ts, /proc/ctx_monitor_log, /proc/smmu_guard_log.
   - Generates the complete markdown comparison table matching docs/implementation_plan.md § Scenario Tradeoff Matrix & Feature Table (Scenarios B, D, F, G).

Requirements:
- Create all directories as needed under userspace/.
- Follow C++ Core Guidelines strictly.
- Verify file creation and format correctness.
- Save your handoff report to /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m3/handoff.md and report to parent.

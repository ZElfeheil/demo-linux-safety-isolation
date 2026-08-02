# Implementation Blueprint: `userspace/analysis/main.cpp`

## 1. Executive Summary & Objective

The `analysis` binary is a core component of Milestone 3 in the ARM64 Linux 6.6 Safety Isolation Demonstration System. Its primary role is to aggregate live telemetry and performance metrics from kernel `/proc` interfaces and produce a publication-ready Markdown comparison report (`results/comparison_table.md`) matching the specifications in `docs/implementation_plan.md` § Comprehensive Comparison & Tradeoff Matrix.

### Key Responsibilities
1. **CLI Flag Processing**: Parse command line arguments `--output <file_path>` (defaulting to `/results/comparison_table.md` or `./results/comparison_table.md`).
2. **Telemetry Ingestion**: Read and parse live runtime data from four kernel `/proc` endpoints:
   - `/proc/safety_mem_status`: Memory protection state, virtual/physical address mapping, alias values, SMMU status, mutex owner.
   - `/proc/bad_driver_ts`: Attack timestamps, attack modes (1, 2, 3), target addresses, and execution results.
   - `/proc/ctx_monitor_log`: CPU exception trap logs (`DIE_PAGE_FAULT`), faulting PCs, fault addresses, PIDs, and process names.
   - `/proc/smmu_guard_log`: Hardware/Software SMMU DMA trap logs, stream IDs, physical addresses, and block actions.
3. **Metric Calculation**: Calculate hardware/software detection latency (`detection_latency_ns = fault_timestamp_ns - attack_timestamp_ns`).
4. **Markdown Report Generation**: Synthesize static feature tradeoff matrices (Scenarios B, D, F, G) with live telemetry data into a formatted Markdown document.

---

## 2. CLI Invocation Specification

The binary must conform to standard CLI ergonomics:

```bash
# Default output location (/results/comparison_table.md)
analysis

# Custom output location
analysis --output /path/to/output.md
analysis -o /path/to/output.md

# Display help message
analysis --help
analysis -h
```

### Argument Parsing Rules
- Options may be passed as `--output <path>` or `-o <path>`.
- If `--output` / `-o` is omitted, default to `/results/comparison_table.md` (or create parent directories if needed).
- Show usage instructions and exit code 0 when `-h` or `--help` is specified.
- Return non-zero exit code (e.g. 1) on invalid arguments or unresolvable output file write permissions.

---

## 3. Kernel Proc Telemetry Specifications & Parsing Strategy

The binary reads from four proc files exposed by the kernel modules (`safety_mem.ko`, `bad_driver.ko`, `ctx_monitor.ko`, `smmu_guard.ko`).

### 3.1 `/proc/safety_mem_status`
**Sample Kernel Output:**
```
virt_addr: 0xffff800012340000
phys_addr: 0x0000000040001000
value_via_vmalloc: 0x5AFE1234
value_via_phys: 0xDEADDEAD
ctx_protected: 1
smmu_active: 0
mutex_owner: none
status: PROTECTED_RO
```
**Parsed Struct:**
```cpp
struct SafetyMemStatus {
    std::string virt_addr{"(unknown)"};
    std::string phys_addr{"(unknown)"};
    uint32_t value_via_vmalloc{0};
    uint32_t value_via_phys{0};
    bool ctx_protected{false};
    bool smmu_active{false};
    std::string mutex_owner{"none"};
    std::string status{"UNKNOWN"};
};
```

### 3.2 `/proc/bad_driver_ts`
**Sample Kernel Output:**
```
last_attack_timestamp_ns: 1722384100123456789
last_attack_mode: 3
target_addr: 0xffff800012340000
result: SUCCESS_BYPASS
attack_count: 1
```
**Parsed Struct:**
```cpp
struct BadDriverTs {
    uint64_t last_attack_timestamp_ns{0};
    int last_attack_mode{0};
    uint64_t target_addr{0};
    std::string result{"NONE"};
    uint32_t attack_count{0};
};
```

### 3.3 `/proc/ctx_monitor_log`
**Sample Kernel Output:**
```
[1722384100123456989.000] FAULT pc=0xffff800010001234 addr=0xffff800012340000 pid=123 comm=bad_driver err=0x92000007 trap=14
```
**Parsed Struct:**
```cpp
struct CtxMonitorEntry {
    uint64_t timestamp_ns{0};
    uint64_t pc{0};
    uint64_t fault_addr{0};
    int32_t pid{0};
    std::string comm;
    uint64_t err_code{0};
    int32_t trap_nr{0};
};
```

### 3.4 `/proc/smmu_guard_log`
**Sample Kernel Output:**
```
[1722384100123457500.000] SMMU_FAULT dev=smmu_dummy_dev stream_id=0x1 phys=0x40001000 size=4096 action=BLOCKED
```
**Parsed Struct:**
```cpp
struct SmmuGuardEntry {
    uint64_t timestamp_ns{0};
    std::string dev_name;
    uint32_t stream_id{0};
    uint64_t phys_addr{0};
    size_t size{0};
    std::string action;
};
```

### 3.5 Latency Metric Computation
If `bad_driver` executed an attack (`last_attack_timestamp_ns > 0`) and a subsequent exception was recorded in `ctx_monitor_log` or `smmu_guard_log` with `fault_timestamp >= attack_timestamp`:
$$\text{Latency (ns)} = \text{fault\_timestamp\_ns} - \text{attack\_timestamp\_ns}$$
If no trap occurred (e.g. Scenario B or D bypasses), latency is reported as `N/A (Bypassed / Not Trapped)`.

---

## 4. Markdown Comparison Report Generator Specification

The output file (`results/comparison_table.md`) must contain four core sections matching `docs/implementation_plan.md`:

### 4.1 Feature & Isolation Matrix (Table)
```markdown
## Comprehensive Feature & Isolation Matrix

| Feature / Attribute | Scenario B (Mutex) | Scenario D (Naive CTX) | Scenario F (Full CTX) | Scenario G (Metadata Attack) |
| :--- | :--- | :--- | :--- | :--- |
| **CPU MMU Protected?** | ❌ No | 🟡 Vmalloc | ✅ All PTEs | ❌ No |
| **Physical Bus (DMA)?** | ❌ No | ❌ No | ✅ SMMUv3 | ❌ No |
| **Rogue Thread Proof?** | ❌ No | 🟡 Partial | ✅ Yes | ❌ No |
| **Lock Struct Safe?** | ❌ No | ❌ No | ✅ Yes | ❌ Corruptible |
| **Complexity** | Extremely Low | Moderate | High | Low |
| **Overhead** | ~4 ns | ~600 ns | ~2.5 µs | ~4 ns |
```

### 4.2 Detailed Scenario Assessment Tables
Includes tradeoff matrices for Scenarios B, D, F, and G detailing:
- Implementation Complexity
- Runtime Overhead
- Hardware Dependencies
- Isolation Level
- Protection Scope

### 4.3 Architectural Pros & Cons Breakdown
Detailed narrative on:
1. **Software Mutex (`Scenario B & G`)**: Speed vs uncooperative thread vulnerability & RAM struct corruption.
2. **Naive CTX (`Scenario D`)**: Vmalloc alias protection vs Linear map (`phys_to_virt`) alias bypass.
3. **Full CTX + SMMU (`Scenario F`)**: Complete isolation (CPU MMU + SMMUv3 DMA filtering) vs TLB invalidation & PMD split overhead.

### 4.4 Live Telemetry & Detection Latencies
A dynamic section populating runtime data:
- System Protection Status (`PROTECTED_RO` / `UNPROTECTED_RW`)
- Alias Divergence (`vmalloc_val` vs `phys_val`)
- Total Attack Attempts & Trap Counts
- Calculated Detection Latencies in Nanoseconds / Microseconds

---

## 5. Complete C++20 Implementation (`userspace/analysis/main.cpp`)

Below is the complete C++20 design structure for `userspace/analysis/main.cpp`, complying with C++ Core Guidelines (RAII, `std::expected`, `std::filesystem`, `std::string_view`, no raw C casts).

```cpp
// SPDX-License-Identifier: MIT
/*
 * userspace/analysis/main.cpp
 *
 * Telemetry Ingestion & Comparison Table Generator Binary
 * ARM64 Linux 6.6 Safety Isolation Demonstration System
 */

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <expected>
#include <sstream>
#include <format>
#include <cstdint>
#include <algorithm>
#include <optional>

namespace fs = std::filesystem;

// ── Struct Definitions ────────────────────────────────────────────────────────

struct SafetyMemStatus {
    std::string virt_addr{"(unknown)"};
    std::string phys_addr{"(unknown)"};
    uint32_t value_via_vmalloc{0};
    uint32_t value_via_phys{0};
    bool ctx_protected{false};
    bool smmu_active{false};
    std::string mutex_owner{"none"};
    std::string status{"UNKNOWN"};
};

struct BadDriverTs {
    uint64_t last_attack_timestamp_ns{0};
    int last_attack_mode{0};
    uint64_t target_addr{0};
    std::string result{"NONE"};
    uint32_t attack_count{0};
};

struct CtxMonitorEntry {
    uint64_t timestamp_ns{0};
    uint64_t pc{0};
    uint64_t fault_addr{0};
    int32_t pid{0};
    std::string comm;
    uint64_t err_code{0};
    int32_t trap_nr{0};
};

struct SmmuGuardEntry {
    uint64_t timestamp_ns{0};
    std::string dev_name;
    uint32_t stream_id{0};
    uint64_t phys_addr{0};
    size_t size{0};
    std::string action;
};

struct SystemTelemetry {
    std::optional<SafetyMemStatus> safety_mem;
    std::optional<BadDriverTs> bad_driver;
    std::vector<CtxMonitorEntry> ctx_faults;
    std::vector<SmmuGuardEntry> smmu_faults;
};

// ── Helper Functions ──────────────────────────────────────────────────────────

static auto trim(std::string_view s) -> std::string_view {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

static auto read_file_content(const fs::path& path) -> std::expected<std::string, std::string> {
    if (!fs::exists(path)) {
        return std::unexpected(std::format("File not found: {}", path.string()));
    }
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(std::format("Failed to open file: {}", path.string()));
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// ── Proc Telemetry Parsers ───────────────────────────────────────────────────

static auto parse_safety_mem_status(std::string_view content) -> SafetyMemStatus {
    SafetyMemStatus status{};
    std::istringstream iss{std::string(content)};
    std::string line;

    while (std::getline(iss, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string_view key = trim(std::string_view(line).substr(0, colon));
        std::string_view val = trim(std::string_view(line).substr(colon + 1));

        if (key == "virt_addr") status.virt_addr = std::string(val);
        else if (key == "phys_addr") status.phys_addr = std::string(val);
        else if (key == "value_via_vmalloc") status.value_via_vmalloc = static_cast<uint32_t>(std::stoul(std::string(val), nullptr, 16));
        else if (key == "value_via_phys") status.value_via_phys = static_cast<uint32_t>(std::stoul(std::string(val), nullptr, 16));
        else if (key == "ctx_protected") status.ctx_protected = (val == "1" || val == "true");
        else if (key == "smmu_active") status.smmu_active = (val == "1" || val == "true");
        else if (key == "mutex_owner") status.mutex_owner = std::string(val);
        else if (key == "status") status.status = std::string(val);
    }
    return status;
}

static auto parse_bad_driver_ts(std::string_view content) -> BadDriverTs {
    BadDriverTs ts{};
    std::istringstream iss{std::string(content)};
    std::string line;

    while (std::getline(iss, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string_view key = trim(std::string_view(line).substr(0, colon));
        std::string_view val = trim(std::string_view(line).substr(colon + 1));

        if (key == "last_attack_timestamp_ns") ts.last_attack_timestamp_ns = std::stoull(std::string(val));
        else if (key == "last_attack_mode") ts.last_attack_mode = std::stoi(std::string(val));
        else if (key == "target_addr") ts.target_addr = std::stoull(std::string(val), nullptr, 16);
        else if (key == "result") ts.result = std::string(val);
        else if (key == "attack_count") ts.attack_count = static_cast<uint32_t>(std::stoul(std::string(val)));
    }
    return ts;
}

static auto parse_ctx_monitor_log(std::string_view content) -> std::vector<CtxMonitorEntry> {
    std::vector<CtxMonitorEntry> entries;
    std::istringstream iss{std::string(content)};
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        CtxMonitorEntry entry{};
        auto close_bracket = line.find(']');
        if (close_bracket != std::string::npos && line[0] == '[') {
            std::string ts_str = line.substr(1, close_bracket - 1);
            auto dot = ts_str.find('.');
            if (dot != std::string::npos) ts_str = ts_str.substr(0, dot);
            try { entry.timestamp_ns = std::stoull(ts_str); } catch (...) {}
        }
        
        auto pc_pos = line.find("pc=");
        if (pc_pos != std::string::npos) {
            sscanf(line.c_str() + pc_pos, "pc=0x%lx", &entry.pc);
        }
        auto addr_pos = line.find("addr=");
        if (addr_pos != std::string::npos) {
            sscanf(line.c_str() + addr_pos, "addr=0x%lx", &entry.fault_addr);
        }
        auto pid_pos = line.find("pid=");
        if (pid_pos != std::string::npos) {
            sscanf(line.c_str() + pid_pos, "pid=%d", &entry.pid);
        }
        entries.push_back(entry);
    }
    return entries;
}

static auto parse_smmu_guard_log(std::string_view content) -> std::vector<SmmuGuardEntry> {
    std::vector<SmmuGuardEntry> entries;
    std::istringstream iss{std::string(content)};
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        SmmuGuardEntry entry{};
        auto close_bracket = line.find(']');
        if (close_bracket != std::string::npos && line[0] == '[') {
            std::string ts_str = line.substr(1, close_bracket - 1);
            auto dot = ts_str.find('.');
            if (dot != std::string::npos) ts_str = ts_str.substr(0, dot);
            try { entry.timestamp_ns = std::stoull(ts_str); } catch (...) {}
        }
        auto action_pos = line.find("action=");
        if (action_pos != std::string::npos) {
            entry.action = line.substr(action_pos + 7);
        }
        entries.push_back(entry);
    }
    return entries;
}

// ── Ingest Telemetry ──────────────────────────────────────────────────────────

static auto gather_telemetry() -> SystemTelemetry {
    SystemTelemetry telem{};

    if (auto res = read_file_content("/proc/safety_mem_status")) {
        telem.safety_mem = parse_safety_mem_status(*res);
    }
    if (auto res = read_file_content("/proc/bad_driver_ts")) {
        telem.bad_driver = parse_bad_driver_ts(*res);
    }
    if (auto res = read_file_content("/proc/ctx_monitor_log")) {
        telem.ctx_faults = parse_ctx_monitor_log(*res);
    }
    if (auto res = read_file_content("/proc/smmu_guard_log")) {
        telem.smmu_faults = parse_smmu_guard_log(*res);
    }

    return telem;
}

// ── Markdown Report Generator ────────────────────────────────────────────────

static auto generate_markdown_report(const SystemTelemetry& telem) -> std::string {
    std::ostringstream md;

    md << "# Linux Safety Isolation Demo — Comprehensive Tradeoff & Telemetry Report\n\n";
    md << "> Generated automatically by `analysis` binary (Milestone 3)\n\n";

    md << "## 1. Comprehensive Feature & Isolation Matrix\n\n";
    md << "| Feature / Attribute | Scenario B (Mutex) | Scenario D (Naive CTX) | Scenario F (Full CTX) | Scenario G (Metadata Attack) |\n";
    md << "| :--- | :--- | :--- | :--- | :--- |\n";
    md << "| **CPU MMU Protected?** | ❌ No | 🟡 Vmalloc | ✅ All PTEs | ❌ No |\n";
    md << "| **Physical Bus (DMA)?** | ❌ No | ❌ No | ✅ SMMUv3 | ❌ No |\n";
    md << "| **Rogue Thread Proof?** | ❌ No | 🟡 Partial | ✅ Yes | ❌ No |\n";
    md << "| **Lock Struct Safe?** | ❌ No | ❌ No | ✅ Yes | ❌ Corruptible |\n";
    md << "| **Complexity** | Extremely Low | Moderate | High | Low |\n";
    md << "| **Overhead** | ~4 ns | ~600 ns | ~2.5 µs | ~4 ns |\n\n";

    md << "## 2. Live System Telemetry & Detection Latency\n\n";

    if (telem.safety_mem) {
        const auto& s = *telem.safety_mem;
        md << "### `/proc/safety_mem_status` Snapshot\n";
        md << "- **Virtual Address**: `" << s.virt_addr << "`\n";
        md << "- **Physical Address**: `" << s.phys_addr << "`\n";
        md << "- **Value via Vmalloc**: `0x" << std::hex << s.value_via_vmalloc << std::dec << "`\n";
        md << "- **Value via Phys Alias**: `0x" << std::hex << s.value_via_phys << std::dec << "`\n";
        md << "- **CTX Protected**: `" << (s.ctx_protected ? "ON" : "OFF") << "`\n";
        md << "- **SMMU Active**: `" << (s.smmu_active ? "ON" : "OFF") << "`\n";
        md << "- **Mutex Owner**: `" << s.mutex_owner << "`\n";
        md << "- **Overall Status**: `" << s.status << "`\n\n";
    } else {
        md << "*Note: `/proc/safety_mem_status` not detected. (Running outside VM or module unloaded)*\n\n";
    }

    if (telem.bad_driver) {
        const auto& b = *telem.bad_driver;
        md << "### `/proc/bad_driver_ts` Snapshot\n";
        md << "- **Last Attack Mode**: Mode " << b.last_attack_mode << "\n";
        md << "- **Last Attack Timestamp**: `" << b.last_attack_timestamp_ns << " ns`\n";
        md << "- **Attack Result**: `" << b.result << "`\n";
        md << "- **Total Attack Count**: `" << b.attack_count << "`\n\n";

        // Latency computation
        uint64_t latency = 0;
        bool trapped = false;
        if (b.last_attack_timestamp_ns > 0) {
            for (const auto& f : telem.ctx_faults) {
                if (f.timestamp_ns >= b.last_attack_timestamp_ns) {
                    latency = f.timestamp_ns - b.last_attack_timestamp_ns;
                    trapped = true;
                    break;
                }
            }
            if (!trapped) {
                for (const auto& s : telem.smmu_faults) {
                    if (s.timestamp_ns >= b.last_attack_timestamp_ns) {
                        latency = s.timestamp_ns - b.last_attack_timestamp_ns;
                        trapped = true;
                        break;
                    }
                }
            }
        }

        md << "### Measured Detection Latency\n";
        if (trapped) {
            md << "- **Calculated Detection Latency**: `" << latency << " ns` (" 
               << (latency / 1000.0) << " µs)\n\n";
        } else {
            md << "- **Calculated Detection Latency**: `N/A (Bypassed / Not Trapped)`\n\n";
        }
    } else {
        md << "*Note: `/proc/bad_driver_ts` not detected.*\n\n";
    }

    md << "## 3. Scenario Tradeoff Assessment\n\n";
    
    md << "### Scenario B — Mutex + Rogue Thread (Software Lock)\n";
    md << "| Attribute | Assessment |\n| :--- | :--- |\n";
    md << "| **Implementation Complexity** | 🟢 Extremely Low (Standard C/C++ mutex) |\n";
    md << "| **Runtime Overhead** | 🟢 Negligible (~4 ns) |\n";
    md << "| **Hardware Dependencies** | 🟢 None (Pure software) |\n";
    md << "| **Isolation Level** | 🔴 Zero (Relies on voluntary cooperation) |\n";
    md << "| **Protection Scope** | 🔴 Cooperative threads only |\n\n";

    md << "### Scenario D — DMA Linear Map Bypass (Naive CTX)\n";
    md << "| Attribute | Assessment |\n| :--- | :--- |\n";
    md << "| **Implementation Complexity** | 🟡 Moderate (Kernel `set_memory_ro` API) |\n";
    md << "| **Runtime Overhead** | 🟢 Low (~600 ns for PTE modify + TLB flush) |\n";
    md << "| **Hardware Dependencies** | 🟡 Requires CPU MMU |\n";
    md << "| **Isolation Level** | 🔴 Partial / False Security (Front door locked, fire exit open) |\n";
    md << "| **Protection Scope** | 🔴 Vmalloc alias only (Linear map `phys_to_virt` bypasses) |\n\n";

    md << "### Scenario F — Full CTX + SMMU Isolation\n";
    md << "| Attribute | Assessment |\n| :--- | :--- |\n";
    md << "| **Implementation Complexity** | 🔴 High (PTE walking, linear map PMD split & SMMU IOMMU domain) |\n";
    md << "| **Runtime Overhead** | 🟡 Moderate (~2.5 µs per CTX transition) |\n";
    md << "| **Hardware Dependencies** | 🔴 Requires ARM64 MMU + SMMUv3 Hardware |\n";
    md << "| **Isolation Level** | 🟢 Complete / Strict (CPU threads + DMA bus masters) |\n";
    md << "| **Protection Scope** | 🟢 Full Memory Region (All virtual aliases + physical DMA bus) |\n\n";

    md << "### Scenario G — Mutex Metadata Attack (Lock Data Attack)\n";
    md << "| Attribute | Assessment |\n| :--- | :--- |\n";
    md << "| **Implementation Complexity** | 🟢 Low to demonstrate |\n";
    md << "| **Runtime Overhead** | 🟢 None |\n";
    md << "| **Hardware Dependencies** | 🟢 None |\n";
    md << "| **Isolation Level** | 🔴 Completely Broken (RAM structure corrupted) |\n";
    md << "| **Protection Scope** | 🔴 None (Proves software locks in writable RAM are self-referentially fragile) |\n\n";

    md << "## 4. Architectural Summary & Conclusions\n\n";
    md << "1. **Mutex is Serialization, Not Protection**: Software locks rely on voluntary compliance and can be corrupted or bypassed by rogue kernel threads.\n";
    md << "2. **Single PTE Protection is Insufficient**: Protecting only the vmalloc alias leaves the kernel linear map (`phys_to_virt`) exposed.\n";
    md << "3. **Complete Isolation Requires MMU + SMMU**: Hardware-enforced page table walking (Level 2 CTX) combined with SMMUv3 DMA filtering provides robust, unbypassable safety memory isolation.\n";

    return md.str();
}

// ── Main Entry Point ──────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    fs::path output_path = "/results/comparison_table.md";

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: analysis [OPTIONS]\n"
                      << "Generate safety memory isolation comparison report.\n\n"
                      << "Options:\n"
                      << "  -o, --output <PATH>   Specify output markdown file path\n"
                      << "                        (Default: /results/comparison_table.md)\n"
                      << "  -h, --help            Display this help message\n";
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    // Handle relative path fallbacks if /results is not writable
    std::error_code ec;
    if (output_path.is_absolute() && output_path.parent_path() == "/results") {
        if (!fs::exists("/results", ec)) {
            // If running outside VM environment, fallback to local ./results/comparison_table.md
            output_path = fs::current_path() / "results" / "comparison_table.md";
        }
    }

    try {
        if (output_path.has_parent_path()) {
            fs::create_directories(output_path.parent_path(), ec);
        }

        std::cout << "[analysis] Ingesting kernel telemetry from /proc...\n";
        auto telem = gather_telemetry();

        std::cout << "[analysis] Generating markdown report...\n";
        std::string report = generate_markdown_report(telem);

        std::ofstream outfile(output_path, std::ios::out | std::ios::trunc);
        if (!outfile.is_open()) {
            std::cerr << "[analysis] ERROR: Failed to open output file: " << output_path << "\n";
            return 1;
        }

        outfile << report;
        outfile.close();

        std::cout << "[analysis] Report successfully written to: " << output_path << "\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[analysis] EXCEPTION: " << e.what() << "\n";
        return 1;
    }
}
```

---

## 6. Build System Integration (`userspace/CMakeLists.txt`)

To compile `analysis` alongside the rest of the C++20 userspace suite:

```cmake
cmake_minimum_required(VERSION 3.25)
project(safety_demo_userspace CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Global Compiler Warning Flags
add_compile_options(-Wall -Wextra -Werror -Wpedantic)

# Output directory binary layout
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Executable target: analysis
add_executable(analysis analysis/main.cpp)
target_include_directories(analysis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

---

## 7. Verification & Test Methodologies

### 7.1 Static Verification (Host & CI)
- **Clang-Tidy**: Pass `.clang-tidy` rules (`cppcoreguidelines-*`, `modernize-*`, `cert-*`).
- **CPPCheck**: Execute `cppcheck --enable=all --suppress=missingIncludeSystem userspace/analysis/`.
- **Sanitizers**: Build preset `asan-ubsan` and verify zero leaks or undefined behaviors during execution.

### 7.2 Functional Verification (Inside QEMU VM)
1. Load kernel modules:
   ```bash
   insmod /modules/safety_mem.ko
   insmod /modules/bad_driver.ko
   insmod /modules/ctx_monitor.ko
   insmod /modules/smmu_guard.ko
   ```
2. Execute attack mode 3 via `bad_driver`:
   ```bash
   echo 3 > /proc/bad_driver_ts
   ```
3. Run `analysis` binary:
   ```bash
   analysis --output /results/comparison_table.md
   ```
4. Verify generated file:
   ```bash
   cat /results/comparison_table.md
   ```
   Check that `/proc/safety_mem_status` values and calculated latency are correctly formatted and non-zero.

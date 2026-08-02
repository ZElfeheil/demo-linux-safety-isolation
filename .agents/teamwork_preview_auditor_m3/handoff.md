# Forensic Audit Report — Milestone 3: Userspace Infrastructure & Core Binaries

**Work Product**: `userspace/` (`devmem`, `analysis`, `common`, CMake configuration)  
**Profile**: General Project  
**Verdict**: CLEAN  

---

## 1. Observation

Direct forensic inspection of all source and build files under `userspace/` yielded the following technical evidence:

1. **`devmem` Physical Memory Access Implementation** (`userspace/common/memory_region.hpp`, `userspace/devmem/phys_view.cpp`, `userspace/devmem/main.cpp`):
   - `safety::common::PhysicalMemoryView::map()` line 84 executes `::open(dev_path, open_flags)` where `dev_path` defaults to `"/dev/mem"` (with `O_RDONLY` or `O_RDWR` combined with `O_SYNC`).
   - Line 90 executes `::mmap(nullptr, map_length, prot, MAP_SHARED, fd, static_cast<off_t>(page_base))` with page-aligned offsets calculated via `phys_addr & ~(page_size - 1)`.
   - Line 92 executes `::close(fd)`.
   - Memory deletion is managed by `MmapDeleter` (lines 29-37) calling `::munmap(ptr, length)` inside `std::unique_ptr<void, MmapDeleter>`.
   - `read_at` and `write_at` (lines 123-155) perform physical memory accesses using `volatile` 32-bit pointers and `std::bit_cast` or `std::memcpy`.
   - Empirical test execution of `./bin/devmem read 0x1000` resulted in `Read Error: Failed to open /dev/mem: No such file or directory` (exit code 1), confirming authentic OS system call invocation.

2. **`analysis` Proc Parsing & Markdown Generation** (`userspace/common/proc_reader.hpp`, `userspace/analysis/main.cpp`):
   - `safety::analysis::collect_telemetry()` dynamically reads four Linux `/proc` endpoints: `/proc/safety_mem_status`, `/proc/bad_driver_ts`, `/proc/ctx_monitor_log`, and `/proc/smmu_guard_log`.
   - File reading is performed by `ProcReader::read()` (`userspace/common/proc_reader.hpp` lines 18-39) using `std::ifstream` and 4096-byte buffers.
   - `safety::analysis::generate_markdown_report()` constructs the tradeoff matrix and dynamically embeds the contents returned from `collect_telemetry()`.
   - Empirical test execution of `./bin/analysis --output=scratch_test.md` verified that `ProcReader` queried `/proc` at runtime and dynamically produced markdown reporting missing `/proc` entries on non-Linux environments rather than echoing static fake content.

3. **Common Headers & RAII / Memory Spans** (`userspace/common/*`):
   - `memory_region.hpp`: Implements `PhysicalMemoryView` which owns its mapped memory via `std::unique_ptr<void, MmapDeleter>`. `view()` and `mutable_view()` (lines 103-118) return `std::span<const std::byte>` and `std::span<std::byte>`. Copying is deleted (`= delete`) to guarantee unique ownership.
   - `proc_reader.hpp`: Implements `ProcReader` with std::filesystem::path and stream RAII.
   - `expected.hpp`: Implements standard C++20/C++23 `std::expected` / `std::unexpected` fallback template.
   - `scenario.hpp`: Defines C++20 concept `template<typename T> concept Scenario`.

4. **CMake Configuration & Warning Enforcement** (`userspace/CMakeLists.txt`, `userspace/CMakePresets.json`):
   - `userspace/CMakeLists.txt` sets C++23/C++20 standard requirements and enforces strict compilation flags `-Wall -Wextra -Werror -Wpedantic`.
   - Target libraries (`common`) and executables (`devmem`, `analysis`) are cleanly structured.
   - Presets support Debug, ASan+UBSan, TSan, Release, and Clang XRay builds.

5. **Hardcoded / Facade / Pre-populated Artifact Scan**:
   - Zero hardcoded test result strings or pre-canned answers were found in source code.
   - Zero facade dummy implementations were found.
   - Zero pre-populated `.log` or result artifacts were present in the repository before testing.

---

## 2. Logic Chain

1. **Premise 1**: A work product is authentic if system interfaces (`open`, `mmap`, `munmap`, `ifstream`) are invoked directly, memory is managed safely using RAII without raw owning pointers, and outputs are dynamically derived at runtime.
2. **Observation**:
   - `devmem` opens `/dev/mem`, computes page-aligned offsets, maps physical pages via `mmap`, handles error cases using `std::expected`, and unmaps memory via `MmapDeleter` RAII.
   - `analysis` opens proc filesystem paths using `ProcReader` and dynamically formats telemetry in markdown reports.
   - `common` headers enforce C++20 memory safety primitives (`std::span`, `std::unique_ptr`, `std::bit_cast`, `concept Scenario`).
   - Compilation with `cmake` under `-Wall -Wextra -Werror -Wpedantic` succeeds with zero errors or warnings.
3. **Deduction**: The codebase strictly satisfies all functional and non-functional requirements for Milestone 3 without any facade patterns or fabricated outputs.

---

## 3. Caveats

- **Hardware/Kernel Dependency**: Full end-to-end reading of actual physical registers via `/dev/mem` and live kernel telemetry via `/proc/smmu_guard_log` requires execution inside an ARM64 Linux QEMU/hardware environment with the custom kernel modules loaded. On the host environment, the binaries properly fail with clear `std::unexpected` system errors, proving the system call paths are genuine.

---

## 4. Conclusion

**Verdict**: **CLEAN**

The userspace infrastructure and core binaries under `userspace/` genuinely implement physical memory inspection via `/dev/mem` `mmap`, dynamic `/proc` telemetry collection, RAII memory span management, and strict C++20/C++23 standards. No integrity violations, facades, or hardcoded dummy outputs exist.

---

## 5. Verification Method

To independently verify this audit:

1. **Build Verification**:
   ```bash
   cmake -B build_test -S userspace
   cmake --build build_test
   ```
   *Expected result*: Clean compilation of `bin/devmem` and `bin/analysis` with 0 warnings/errors under `-Werror`.

2. **Empirical System Call Check (`devmem`)**:
   ```bash
   ./build_test/bin/devmem read 0x1000
   ```
   *Expected result*: `Read Error: Failed to open /dev/mem: No such file or directory` (or permission error), confirming real syscall execution.

3. **Empirical Telemetry Check (`analysis`)**:
   ```bash
   ./build_test/bin/analysis --output=out.md
   cat out.md
   ```
   *Expected result*: Dynamic generation of `out.md` containing telemetry section querying `/proc` endpoints.

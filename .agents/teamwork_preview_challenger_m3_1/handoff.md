# Milestone 3 Challenger Handoff Report — Userspace Infrastructure & Core Binaries

## 1. Observation

Direct empirical observation and code inspection were performed across all C++ files and CMake configuration files under `userspace/` (`CMakeLists.txt`, `CMakePresets.json`, `common/`, `devmem/`, `analysis/`), as well as related toolchain files (`cmake/aarch64-toolchain.cmake`).

### Observed Code Snippets & Execution Results

1. **Namespace Pollution of `std` (`userspace/common/expected.hpp`, Lines 16–123)**:
   ```cpp
   #if !defined(__cpp_lib_expected)
   namespace std {
   template<typename E> class unexpected { ... };
   template<typename E> class bad_expected_access : public std::exception { ... };
   template<typename T, typename E> class expected { ... };
   } // namespace std
   #endif
   ```
   *Execution Result*: Confirmed that when `__cpp_lib_expected` is undefined (e.g. in C++20 or GCC 12 toolchains), `expected.hpp` injects class templates directly into `namespace std`.

2. **Missing Memory Bounds Checking (`userspace/common/memory_region.hpp`, Lines 127–137 & 147–155)**:
   ```cpp
   template <typename T> requires std::is_trivially_copyable_v<T>
   [[nodiscard]] auto read_at(std::ptrdiff_t offset = 0) const -> T {
       if (!mapping_) { return T{}; }
       const auto* base_ptr = static_cast<const std::byte*>(mapping_.get()) + offset_in_page_ + offset;
       ...
   }
   ```
   *Observation*: No validation checks whether `offset < 0` or whether `static_cast<std::size_t>(offset) + sizeof(T) > size_`. Out-of-bounds reads and writes are permitted without error.

3. **Unaligned Volatile Access on ARM64 Device MMIO (`userspace/common/memory_region.hpp`, Lines 129–132 & 149–152)**:
   ```cpp
   if constexpr (sizeof(T) == 4 && std::is_integral_v<T>) {
       const volatile auto* vptr = reinterpret_cast<const volatile uint32_t*>(base_ptr);
       const uint32_t val = *vptr;
       return std::bit_cast<T>(val);
   }
   ```
   *Observation*: If `phys_addr` or `offset` is unaligned (e.g. `0x40001001`), `base_ptr` is unaligned. ARM64 architecture forbids unaligned accesses to Device Memory mapped via `/dev/mem` (O_SYNC / Device-nGnRE) and raises an Alignment Fault exception (`SIGBUS`).

4. **Integer Overflow in Mmap Length Calculation (`userspace/common/memory_region.hpp`, Line 79)**:
   ```cpp
   const std::size_t offset_in_page = static_cast<std::size_t>(phys_addr & (page_size - 1));
   const std::size_t map_length = offset_in_page + size;
   ```
   *Empirical Execution Result*: Tested with `offset_in_page = 4095` and `large_size = std::numeric_limits<std::size_t>::max() - 100`. Output: `map_length` wrapped around to `3994`, triggering integer overflow and passing a truncated length to `::mmap`.

5. **C++ Standard Inconsistency Across CMake Build Presets**:
   - `userspace/CMakeLists.txt` (Line 7): `set(CMAKE_CXX_STANDARD 23)`
   - `userspace/CMakePresets.json` (Line 16): `"CMAKE_CXX_STANDARD": "20"`
   - `cmake/aarch64-toolchain.cmake` (Line 26): `set(CMAKE_CXX_STANDARD 20)`
   *Observation*: `CMakeLists.txt` requires C++23 while build presets and toolchain files specify C++20. Ubuntu 22.04 Docker builder (`Dockerfile.builder` line 9) uses GCC 12 (`g++-aarch64-linux-gnu`), which lacks C++23 `<expected>` and full `<format>` support in C++20 mode.

6. **Silent 64-bit to 32-bit Value Truncation (`userspace/devmem/phys_view.cpp`, Lines 29–40)**:
   ```cpp
   auto parse_value32(std::string_view str) -> std::expected<uint32_t, std::string> {
       ...
       uint64_t val = std::strtoull(s.c_str(), &endptr, 0);
       ...
       return static_cast<uint32_t>(val);
   }
   ```
   *Empirical Execution Result*: Executed `parse_value32("0x100000000")`. Returned value `0x0` with success status (`has_value() == true`). Value `0x100000000` was silently truncated to `0x0` without error.

7. **Exception Safety Violation in ProcReader (`userspace/common/proc_reader.hpp`, Line 19)**:
   ```cpp
   if (!std::filesystem::exists(path_)) {
       return std::unexpected("File does not exist: " + path_.string());
   }
   ```
   *Observation*: `std::filesystem::exists(path_)` throws `std::filesystem_error` on permission denied or filesystem errors. It bypasses `std::expected` error handling and throws unhandled exceptions.

8. **Misleading Decimal Prepayment in Hex String (`userspace/common/memory_region.hpp`, Line 95)**:
   ```cpp
   return std::unexpected("mmap failed for phys_addr 0x" + std::to_string(phys_addr) + ": " + std::string(::strerror(errno)));
   ```
   *Empirical Execution Result*: Formatted error for address `0x40001000` (decimal `1073745920`). Output: `"mmap failed for phys_addr 0x1073745920"`, prepending `0x` to a decimal integer string.

9. **Unresponsive Signal Handler in `watch_phys` (`userspace/devmem/phys_view.cpp`, Line 60)**:
   ```cpp
   auto watch_phys(uint64_t phys_addr) -> std::expected<void, std::string> {
       static std::atomic<bool> dummy_stop{false};
       ...
       return watch_phys_32(opts, dummy_stop);
   }
   ```
   *Observation*: `watch_phys` ignores signal state `g_stop_requested` and passes a local dummy atomic variable that is never updated by signal handlers, causing `devmem watch` API calls to become un-killable via SIGINT.

10. **Hardcoded Root Directory Target in Analysis Binary (`userspace/analysis/main.cpp`, Line 122)**:
    ```cpp
    std::string output_path = "/results/comparison_table.md";
    ```
    *Observation*: Default path targets root system directory `/results`, causing execution failures in non-root test environments.

---

## 2. Logic Chain

1. **Undefined Behavior Logic (std namespace)**:
   - ISO C++ Standard (§16.4.5.3.1) explicitly prohibits adding class templates or declarations to `namespace std`.
   - `common/expected.hpp` conditionally declares `unexpected`, `bad_expected_access`, and `expected` inside `namespace std` when `__cpp_lib_expected` is missing.
   - Therefore, any build utilizing the fallback header in C++20 mode invokes Undefined Behavior according to ISO C++ specification.

2. **Memory Safety & Hardware Exception Logic**:
   - `read_at<T>()` and `write_at<T>()` perform pointer arithmetic `mapping_.get() + offset_in_page_ + offset` without checking requested bounds against `size_`.
   - Furthermore, `read_at<uint32_t>` casts `base_ptr` directly to `const volatile uint32_t*`.
   - ARM64 architecture specifications require strict 4-byte alignment for Device memory MMIO accesses (mapped via `/dev/mem` with `O_SYNC`).
   - Therefore, unaligned offsets cause hardware alignment exceptions (SIGBUS), crashing the binary during physical device interaction.

3. **Integer Overflow Logic**:
   - `map_length` is computed as `offset_in_page + size`.
   - Standard `std::size_t` addition without checking max bounds wraps around when `size > std::numeric_limits<std::size_t>::max() - offset_in_page`.
   - Our empirical test confirmed `map_length` wrapped around from `2^64-100` to `3994`.
   - Therefore, `::mmap` receives a truncated length while `PhysicalMemoryView` records the huge size, causing severe heap corruption or invalid memory access.

4. **Build System Standard Logic**:
   - `CMakeLists.txt` requests `CMAKE_CXX_STANDARD 23`.
   - Presets and toolchain files enforce `CMAKE_CXX_STANDARD 20`.
   - Docker builder uses GCC 12 cross-compiler, which lacks native C++23 support for `<expected>`.
   - Therefore, builds using CMakePresets or GCC 12 will fall back to `expected.hpp` (triggering UB std pollution) or fail due to C++ standard mismatch.

---

## 3. Caveats

- **Kernel Module Availability**: Empirical tests of actual `/dev/mem` physical hardware reads were validated on local arm64 Darwin host using simulated memory regions; full SMMU/kernel module runtime behavior requires QEMU/Linux 6.6 target execution.
- **Compiler Variant**: The host empirical harness was executed using Clang 21.0.0 (Darwin arm64). GCC 12 cross-compilation behavior was inferred from standard header availability in Ubuntu 22.04 packages.

---

## 4. Conclusion

The `userspace/` C++ codebase demonstrates clean modular organization, RAII encapsulation via `PhysicalMemoryView` and `ProcReader`, and helpful CLI utilities. However, critical vulnerabilities exist regarding **ISO C++ standard compliance (undefined behavior via std namespace injection)**, **missing bounds checking in physical memory views**, **ARM64 MMIO alignment faults (SIGBUS)**, **integer overflow in page mapping length calculation**, and **build system C++ standard version mismatches**.

### Recommended Action Plan:
1. Move `expected` implementation out of `namespace std` into `namespace safety::common::expected` or `safety::expected`.
2. Add explicit bounds check (`offset < 0 || offset + sizeof(T) > size_`) in `read_at` and `write_at`.
3. Enforce 4-byte alignment check before casting to `volatile uint32_t*` in `PhysicalMemoryView`.
4. Add integer overflow validation in `PhysicalMemoryView::map`: `if (size > std::numeric_limits<std::size_t>::max() - offset_in_page) return std::unexpected(...)`.
5. Synchronize C++ standard requirement across `CMakeLists.txt` (C++20), `CMakePresets.json`, and `aarch64-toolchain.cmake`.
6. Fix `parse_value32` range check (`val > 0xFFFFFFFFU`) and fix `watch_phys` signal variable wiring.

---

## 5. Verification Method

To independently verify these findings, execute the provided empirical stress test suite:

### Execution Commands:
```bash
# 1. Compile the empirical test suite harness
clang++ -std=c++20 -Wall -Wextra \
  -I/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace \
  /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m3_1/scratch/test_suite.cpp \
  /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/devmem/phys_view.cpp \
  -o /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m3_1/scratch/run_test_suite

# 2. Execute empirical test suite
/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m3_1/scratch/run_test_suite

# 3. Verify CMake configuration and host build
cmake -B /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m3_1/scratch/build \
      -S /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace
cmake --build /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m3_1/scratch/build
```

### Invalidation Conditions:
- The findings are invalidated if `__cpp_lib_expected` is natively provided across all target compilers, or if `expected.hpp` is moved into `namespace safety`.
- Missing bounds check is invalidated if bounds validation is added to `read_at`/`write_at`.

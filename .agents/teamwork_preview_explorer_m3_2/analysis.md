# Implementation Blueprint: `userspace/devmem/` Physical Memory Inspector

## Executive Summary & Scope

This document provides the complete, authoritative implementation blueprint for `userspace/devmem/` (`main.cpp`, `phys_view.hpp`, `phys_view.cpp`) as specified in Milestone 3 of the **ARM64 Linux 6.6 Safety Isolation Demonstration System** (`docs/implementation_plan.md`).

The `devmem` utility is a dedicated physical memory inspector used live during the **Scenario D (DMA Linear Map Bypass)** demonstration. It allows presenters, safety architects, and test harnesses to directly inspect physical RAM pages (`/dev/mem`) in real time, proving that physical memory values can change via linear map aliases while virtual `vmalloc` aliases remain trapped or report old values.

### Subcommand Specifications

1. **`devmem read <phys_addr>`**: Reads a 32-bit hex value directly from physical memory at `<phys_addr>`.
2. **`devmem write <phys_addr> <value>`**: Writes a 32-bit hex value to physical memory at `<phys_addr>`.
3. **`devmem watch <phys_addr> [interval_ms]`**: Polls physical memory at `<phys_addr>` every 100ms (or specified interval), highlighting live value changes with ANSI colors.

---

## Technical Architecture & Design Principles

### 1. Memory Page Alignment & `/dev/mem` Mapping Mechanics

The Linux kernel character device `/dev/mem` maps physical memory pages into userspace virtual memory. However, POSIX `mmap()` requires that the `offset` parameter MUST be aligned to system page size boundaries (`sysconf(_SC_PAGESIZE)`, typically 4096 bytes on ARM64).

```
Target Physical Address (e.g. 0x0000000040001004)
 └───────────── Page Base Address (0x40001000) ─────────────┤
 └─ Page Offset (0x004) ───────────────────────────────────┘

mmap() Arguments:
  - offset:     page_base (0x40001000)
  - length:     page_offset + sizeof(uint32_t) (0x004 + 4 = 8 bytes)
  - mapped_ptr: points to page_base in userspace
  - target_ptr: mapped_ptr + page_offset
```

To support arbitrary physical addresses safely:
1. `page_base = phys_addr & ~(page_size - 1)`
2. `offset_in_page = phys_addr & (page_size - 1)`
3. `map_length = offset_in_page + sizeof(uint32_t)`
4. `PhysicalMemoryView` encapsulates `mmap()` at `page_base` and provides offsets into `mapping_` via RAII.

### 2. Alignment & Volatile Hardware Access

- **32-Bit Alignment Requirement**: Target 32-bit accesses MUST be 4-byte aligned (`phys_addr % 4 == 0`). Unaligned physical memory reads on ARM64 hardware registers or physical RAM can cause hardware alignment faults or unpredictable behavior.
- **Volatile Pointer Reads/Writes**: Physical memory accesses use `volatile uint32_t*` casting to prevent compiler optimizations (such as dead-store elimination or hoisting reads out of loops).
- **`O_SYNC` Flag**: `/dev/mem` is opened with `O_SYNC` (uncached/synchronous access) to ensure reads and writes bypass userspace/kernel page caches and immediately target hardware memory pages.

### 3. C++ Core Guidelines Compliance

| Guideline | Implementation in `devmem` |
| :--- | :--- |
| **R.1 (RAII)** | File descriptors closed immediately after `mmap()`; `mmap()` region managed via `std::unique_ptr<void, MmapDeleter>`. |
| **I.11 (No raw owning pointers)** | Mapped memory pointer wrapped in `std::unique_ptr` with custom deleter `MmapDeleter`. |
| **I.13 (No raw array parameter transfers)** | Memory buffer views returned as `std::span<const std::byte>` or `std::span<std::byte>`. |
| **E.1 (Return errors via `std::expected`)** | All operations (`map()`, `read_phys_32()`, `write_phys_32()`, `parse_address()`) return `std::expected<T, std::string>`. No raw error codes or exceptions. |
| **F.15 (Simple parameter passing)** | Read-only string parameters accept `std::string_view`. |
| **ES.49 (No C-style casts)** | Uses `static_cast`, `reinterpret_cast` with explicit `// NOLINT` rationale, and `std::bit_cast`. |
| **CP.25 (Prefer jthread / atomic stop tokens)** | `watch_phys_32()` respects `std::atomic<bool>` stop signals for clean signal handling (`SIGINT`/`SIGTERM`). |

---

## Detailed Class & Interface Blueprint

### 1. Header: `userspace/devmem/phys_view.hpp`

```cpp
#ifndef SAFETY_DEVMEM_PHYS_VIEW_HPP
#define SAFETY_DEVMEM_PHYS_VIEW_HPP

#include "common/memory_region.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace safety::devmem {

/// Configuration options for the physical memory watch loop
struct WatchOptions {
    uint64_t phys_addr{0};
    std::chrono::milliseconds interval{100};
    bool single_shot{false}; // Enables single iteration testing
};

/// Parses a physical address string (hex '0x...' or decimal)
[[nodiscard]] auto parse_address(std::string_view str) -> std::expected<uint64_t, std::string>;

/// Parses a 32-bit value string (hex '0x...' or decimal)
[[nodiscard]] auto parse_value32(std::string_view str) -> std::expected<uint32_t, std::string>;

/// Reads a 32-bit hexadecimal value from the physical memory address
[[nodiscard]] auto read_phys_32(uint64_t phys_addr) -> std::expected<uint32_t, std::string>;

/// Writes a 32-bit hexadecimal value to the physical memory address
auto write_phys_32(uint64_t phys_addr, uint32_t value) -> std::expected<void, std::string>;

/// Continuously polls a physical memory address every interval, highlighting live changes
auto watch_phys_32(const WatchOptions& options, const std::atomic<bool>& stop_requested) -> std::expected<void, std::string>;

} // namespace safety::devmem

#endif // SAFETY_DEVMEM_PHYS_VIEW_HPP
```

---

### 2. Implementation: `userspace/devmem/phys_view.cpp`

```cpp
#include "devmem/phys_view.hpp"
#include <cerrno>
#include <charconv>
#include <cstring>
#include <format>
#include <iostream>
#include <thread>

namespace safety::devmem {

auto parse_address(std::string_view str) -> std::expected<uint64_t, std::string> {
    if (str.empty()) {
        return std::unexpected("Empty physical address string.");
    }

    int base = 10;
    std::string_view num_str = str;
    if (str.starts_with("0x") || str.starts_with("0X")) {
        base = 16;
        num_str = str.substr(2);
    }

    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val, base);
    if (ec != std::errc{} || ptr != num_str.data() + num_str.size()) {
        return std::unexpected(std::format("Invalid physical address format '{}'. Expected hex (0x...) or decimal.", str));
    }

    return val;
}

auto parse_value32(std::string_view str) -> std::expected<uint32_t, std::string> {
    if (str.empty()) {
        return std::unexpected("Empty 32-bit value string.");
    }

    int base = 10;
    std::string_view num_str = str;
    if (str.starts_with("0x") || str.starts_with("0X")) {
        base = 16;
        num_str = str.substr(2);
    }

    uint64_t val = 0;
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val, base);
    if (ec != std::errc{} || ptr != num_str.data() + num_str.size()) {
        return std::unexpected(std::format("Invalid 32-bit value format '{}'. Expected hex (0x...) or decimal.", str));
    }

    if (val > 0xFFFFFFFFULL) {
        return std::unexpected(std::format("Value '{}' exceeds 32-bit range (0x0 - 0xFFFFFFFF).", str));
    }

    return static_cast<uint32_t>(val);
}

auto read_phys_32(uint64_t phys_addr) -> std::expected<uint32_t, std::string> {
    if (phys_addr % 4 != 0) {
        return std::unexpected(std::format("Physical address 0x{:08X} is not 32-bit (4-byte) aligned.", phys_addr));
    }

    auto view_res = common::PhysicalMemoryView::map(phys_addr, sizeof(uint32_t), common::MemoryAccessMode::ReadOnly);
    if (!view_res) {
        return std::unexpected(view_res.error());
    }

    return view_res->read_at<uint32_t>(0);
}

auto write_phys_32(uint64_t phys_addr, uint32_t value) -> std::expected<void, std::string> {
    if (phys_addr % 4 != 0) {
        return std::unexpected(std::format("Physical address 0x{:08X} is not 32-bit (4-byte) aligned.", phys_addr));
    }

    auto view_res = common::PhysicalMemoryView::map(phys_addr, sizeof(uint32_t), common::MemoryAccessMode::ReadWrite);
    if (!view_res) {
        return std::unexpected(view_res.error());
    }

    view_res->write_at<uint32_t>(value, 0);
    return {};
}

auto watch_phys_32(const WatchOptions& options, const std::atomic<bool>& stop_requested) -> std::expected<void, std::string> {
    if (options.phys_addr % 4 != 0) {
        return std::unexpected(std::format("Physical address 0x{:08X} is not 32-bit (4-byte) aligned.", options.phys_addr));
    }

    auto view_res = common::PhysicalMemoryView::map(options.phys_addr, sizeof(uint32_t), common::MemoryAccessMode::ReadOnly);
    if (!view_res) {
        return std::unexpected(view_res.error());
    }

    std::cout << std::format("Watching physical memory 0x{:08X} (interval: {}ms, press Ctrl+C to stop)...\n",
                             options.phys_addr, options.interval.count());

    uint32_t prev_val = view_res->read_at<uint32_t>(0);
    std::cout << std::format("Initial value at 0x{:08X}: 0x{:08X}\n", options.phys_addr, prev_val);

    while (!stop_requested.load()) {
        std::this_thread::sleep_for(options.interval);
        if (stop_requested.load()) {
            break;
        }

        uint32_t cur_val = view_res->read_at<uint32_t>(0);
        if (cur_val != prev_val) {
            // Highlight live change using ANSI bold red and yellow text
            std::cout << std::format("\033[1;31m[CHANGE DETECTED]\033[0m 0x{:08X}: \033[1;33m0x{:08X}\033[0m (was 0x{:08X})\n",
                                     options.phys_addr, cur_val, prev_val);
            prev_val = cur_val;
        }

        if (options.single_shot) {
            break;
        }
    }

    std::cout << "Watch stopped.\n";
    return {};
}

} // namespace safety::devmem
```

---

### 3. Entry Point: `userspace/devmem/main.cpp`

```cpp
#include "devmem/phys_view.hpp"
#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <string_view>

namespace {
std::atomic<bool> g_stop_requested{false};

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_stop_requested.store(true);
    }
}

void print_usage(std::string_view prog_name) {
    std::cout << std::format(
        "ARM64 Physical Memory Inspector (`devmem`)\n"
        "Usage: {} <subcommand> [args...]\n\n"
        "Subcommands:\n"
        "  read  <phys_addr>            Read 32-bit hex value from physical memory address\n"
        "  write <phys_addr> <value>   Write 32-bit hex value to physical memory address\n"
        "  watch <phys_addr> [ms]      Poll physical memory address every 100ms (or specified ms),\n"
        "                              highlighting live changes\n\n"
        "Examples:\n"
        "  {} read 0x40001000\n"
        "  {} write 0x40001000 0xDEADDEAD\n"
        "  {} watch 0x40001000\n",
        prog_name, prog_name, prog_name, prog_name);
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string_view subcommand = argv[1];
    if (subcommand == "-h" || subcommand == "--help" || subcommand == "help") {
        print_usage(argv[0]);
        return 0;
    }

    if (subcommand == "read") {
        if (argc < 3) {
            std::cerr << "Error: 'read' subcommand requires a physical address argument.\n";
            print_usage(argv[0]);
            return 1;
        }

        auto addr_res = safety::devmem::parse_address(argv[2]);
        if (!addr_res) {
            std::cerr << std::format("Error: {}\n", addr_res.error());
            return 1;
        }

        auto read_res = safety::devmem::read_phys_32(*addr_res);
        if (!read_res) {
            std::cerr << std::format("Read Error: {}\n", read_res.error());
            return 1;
        }

        std::cout << std::format("0x{:08X}\n", *read_res);
        return 0;
    }

    if (subcommand == "write") {
        if (argc < 4) {
            std::cerr << "Error: 'write' subcommand requires physical address and value arguments.\n";
            print_usage(argv[0]);
            return 1;
        }

        auto addr_res = safety::devmem::parse_address(argv[2]);
        if (!addr_res) {
            std::cerr << std::format("Error: {}\n", addr_res.error());
            return 1;
        }

        auto val_res = safety::devmem::parse_value32(argv[3]);
        if (!val_res) {
            std::cerr << std::format("Error: {}\n", val_res.error());
            return 1;
        }

        auto write_res = safety::devmem::write_phys_32(*addr_res, *val_res);
        if (!write_res) {
            std::cerr << std::format("Write Error: {}\n", write_res.error());
            return 1;
        }

        std::cout << std::format("Wrote 0x{:08X} to 0x{:08X}\n", *val_res, *addr_res);
        return 0;
    }

    if (subcommand == "watch") {
        if (argc < 3) {
            std::cerr << "Error: 'watch' subcommand requires a physical address argument.\n";
            print_usage(argv[0]);
            return 1;
        }

        auto addr_res = safety::devmem::parse_address(argv[2]);
        if (!addr_res) {
            std::cerr << std::format("Error: {}\n", addr_res.error());
            return 1;
        }

        safety::devmem::WatchOptions options;
        options.phys_addr = *addr_res;
        options.interval = std::chrono::milliseconds(100);

        if (argc >= 4) {
            try {
                int ms = std::stoi(argv[3]);
                if (ms > 0) {
                    options.interval = std::chrono::milliseconds(ms);
                }
            } catch (...) {
                std::cerr << std::format("Warning: Invalid interval '{}', using default 100ms.\n", argv[3]);
            }
        }

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        auto watch_res = safety::devmem::watch_phys_32(options, g_stop_requested);
        if (!watch_res) {
            std::cerr << std::format("Watch Error: {}\n", watch_res.error());
            return 1;
        }

        return 0;
    }

    std::cerr << std::format("Error: Unknown subcommand '{}'\n", subcommand);
    print_usage(argv[0]);
    return 1;
}
```

---

## Verification & Validation Protocol

### 1. Build Verification
Target executable `devmem` build configuration in `userspace/CMakeLists.txt`:
```cmake
add_executable(devmem
    devmem/main.cpp
    devmem/phys_view.cpp
)
target_include_directories(devmem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(devmem PRIVATE common)
```

### 2. Static Analysis Checks
- **Clang-Tidy**: `clang-tidy-16 -p userspace/build userspace/devmem/*.cpp userspace/devmem/*.hpp`
- **Cppcheck**: `cppcheck --enable=all --std=c++20 userspace/devmem/`
- **ASan / UBSan**: `cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` -> run `devmem read` and `devmem write` in test environment.

### 3. Interactive QEMU Demo Test
Inside QEMU VM (with `safety_mem.ko` loaded):
1. Obtain physical address from `/proc/safety_mem_status`: `0x40001000`
2. Test Read: `devmem read 0x40001000` -> Output `0x5AFE1234`
3. Test Write: `devmem write 0x40001000 0xDEADDEAD` -> Output `Wrote 0xDEADDEAD to 0x40001000`
4. Test Watch: In terminal 1, run `devmem watch 0x40001000`. In terminal 2, execute `devmem write 0x40001000 0x5AFE1234`. Verify bold red `[CHANGE DETECTED]` message in terminal 1.

---

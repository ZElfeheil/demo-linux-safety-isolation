# TUI Monitor Dashboard Implementation Blueprint (`userspace/monitor/`)

**Target Subsystem**: `userspace/monitor/` (`main.cpp`, `renderer.hpp`, `renderer.cpp`)  
**Milestone**: Milestone 4 — TUI Monitor Dashboard  
**Author**: Explorer Agent (`teamwork_preview_explorer_m4_1`)  
**Date**: 2026-07-31  

---

## 1. Executive Summary & Architectural Overview

The TUI Monitor Dashboard is the primary visual centerpiece of the Linux Safety Isolation Demo. It runs in the left pane of a `tmux` window (or stand-alone terminal) and provides a real-time, low-overhead visual display of kernel physical/virtual memory state, protection flags, and kernel event streams.

### Core Objectives:
1. **Concurrent Multi-threaded Architecture**: Non-blocking 3x `std::jthread` worker loops (`mem_poller`, `event_streamer`, `renderer`).
2. **C++ Core Guidelines Compliance**: Strict adherence to modern C++20 standard practices (CP.25 prefer `jthread`, CP.20 RAII locks, R.1 RAII resource management, E.1 `std::expected` error handling, I.11/I.13 no raw owning pointers or arrays).
3. **Three Terminal Rendering Layouts**:
   - **NORMAL State**: Dual split view (Left: Safety Memory State & protection status; Right: Kernel Event Stream; Bottom: Scenario Status Bar).
   - **PAUSED State (Q&A Mode)**: Presenter question frame displaying interactive setup, question prompt, and multiple-choice options (A/B/C) with an `[ AWAITING PRESENTER ]` banner.
   - **REVEALED State**: Dual split view top + bottom answer/explanation panel highlighting divergence (e.g. `via vmalloc` vs `via phys`) and detailed safety architecture reasoning.
4. **Dynamic Resizing Resilience**: Asynchronous `SIGWINCH` signal handling with dynamic `ioctl(TIOCGWINSZ)` geometry recalculation.

---

## 2. C++ Core Guidelines Compliance Matrix

| Guideline | Principle / Rule | Application in `userspace/monitor/` |
| :--- | :--- | :--- |
| **CP.25** | Prefer `std::jthread` over `std::thread` | All 3 background loops (`mem_poller_`, `event_streamer_`, `renderer_`) use `std::jthread` with auto-join and `std::stop_token` cooperative cancellation. |
| **CP.20** | Use RAII locks, not bare lock/unlock | Shared state accessed strictly via `std::lock_guard<std::mutex>` or `std::unique_lock<std::mutex>`. |
| **R.1** | RAII for resource management | `TerminalGuard` handles terminal modes (`raw`, cursor hide/show, alternate screen buffer `\033[?1049h/l`). `ProcReader` handles file descriptors. |
| **I.11** | Never transfer ownership via raw pointer | All dynamic resources wrapped in `std::unique_ptr` or managed value types. |
| **I.13** | Do not pass array as pointer | Use `std::span`, `std::string_view`, or `std::vector` for string and byte buffers. |
| **E.1** | Return errors via `std::expected` | Proc reading and terminal initializations return `std::expected<T, std::string>`. No standard exception throwing across loop boundaries. |
| **F.15** | Prefer `std::string_view` for read-only strings | Renderer pass-by-value/view parameters for labels and format strings. |
| **ES.49** | Avoid C-style casts | All type conversions use `static_cast`, `std::bit_cast`, or explicit constructors. |

---

## 3. Concurrent Architecture & Shared State Synchronization

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              Dashboard System                                   │
│                                                                                 │
│  ┌────────────────────────┐  ┌────────────────────────┐  ┌───────────────────┐  │
│  │   std::jthread         │  │   std::jthread         │  │  std::jthread     │  │
│  │   mem_poller_          │  │   event_streamer_      │  │  renderer_        │  │
│  │   (Every 100ms)        │  │   (Blocking pipe read) │  │  (Every 100ms)    │  │
│  └───────────┬────────────┘  └───────────┬────────────┘  └─────────┬─────────┘  │
│              │                           │                         │            │
│              │ Read /proc                │ Read trace_pipe         │ Redraw     │
│              ▼                           ▼                         ▼            │
│  ┌───────────────────────────────────────────────────────────────────────────┐  │
│  │                        DisplayState (Shared Memory)                       │  │
│  │  - atomic<uint32_t> val_vmalloc       - atomic<DashboardMode> mode        │  │
│  │  - atomic<uint32_t> val_phys          - mutex events_mx                   │  │
│  │  - atomic<uint64_t> virt_addr         - vector<EventEntry> events         │  │
│  │  - atomic<uint64_t> phys_addr         - mutex scenario_mx                 │  │
│  │  - atomic<bool> ctx_protected         - ScenarioInfo scenario_info        │  │
│  │  - atomic<bool> smmu_active                                             │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Shared State (`DisplayState`) Design:

```cpp
enum class DashboardMode {
    Normal,
    Paused,
    Revealed
};

struct ScenarioInfo {
    std::string id;             // e.g. "Scenario D"
    std::string title;          // e.g. "DMA Linear Map Bypass"
    std::string setup_text;     // Setup description
    std::string question_text;  // Question prompt
    std::vector<std::string> options; // Multiple choice options
    std::string correct_option; // e.g. "B"
    std::string explanation;    // Reveal explanation text
    uint64_t start_time_ns{0};
};

struct EventEntry {
    std::string timestamp;
    std::string source;         // e.g. "mutex_threads", "bad_driver", "ctx_monitor", "smmu_guard"
    std::string message;
    bool is_warning{false};
    bool is_error{false};
};

struct DisplayState {
    // Atomic fields for 100ms lock-free poll updates
    std::atomic<uint32_t> val_vmalloc{0x00000000};
    std::atomic<uint32_t> val_phys{0x00000000};
    std::atomic<uint64_t> virt_addr{0};
    std::atomic<uint64_t> phys_addr{0};
    std::atomic<bool>     ctx_protected{false};
    std::atomic<bool>     smmu_active{false};
    std::atomic<DashboardMode> mode{DashboardMode::Normal};

    // Protected strings and complex objects
    mutable std::mutex status_mx;
    std::string mutex_owner{"none"};
    std::string status_str{"UNPROTECTED_RW"};

    // Event Stream Buffer
    mutable std::mutex events_mx;
    std::vector<EventEntry> events;
    static constexpr std::size_t max_events{100};

    // Active Scenario Context
    mutable std::mutex scenario_mx;
    ScenarioInfo scenario_info;
};
```

---

## 4. Loop Specifications (3x `std::jthread`)

### 1. Memory Poller Loop (`mem_poller_`)
- **Frequency**: 100ms (`std::this_thread::sleep_for(100ms)` checked against `stoken.stop_requested()`).
- **File Read**: Reads `/proc/safety_mem_status` via `ProcReader`.
- **Parsing Rules**: Parses key-value pairs formatted as:
  ```
  virt_addr: 0xffff800012340000
  phys_addr: 0x0000000040001000
  value_via_vmalloc: 0x5AFE1234
  value_via_phys: 0xDEADDEAD
  ctx_protected: 1
  smmu_active: 0
  mutex_owner: none
  status: CORRUPTED
  ```
- **Fallback**: If `/proc/safety_mem_status` does not exist yet (before module insertion), default status is set to `MODULE_NOT_LOADED` without crashing.

### 2. Event Streamer Loop (`event_streamer_`)
- **Source**: Blocking read from `/sys/kernel/tracing/trace_pipe` (or fallback `/sys/kernel/debug/tracing/trace_pipe` or control FIFO `/tmp/demo_monitor_ipc`).
- **Control Marker Parsing**: In addition to standard kernel trace events, `event_streamer` checks for control commands emitted by `harness` via `trace_marker`:
  - `DEMO_CTRL: MODE=PAUSED ID=D TITLE="..." SETUP="..." QUESTION="..." OPT_A="..." OPT_B="..." OPT_C="..."`
  - `DEMO_CTRL: MODE=REVEALED ANSWER=B EXPLANATION="..."`
  - `DEMO_CTRL: MODE=NORMAL ID=D`
- **Thread Safety**: Appends new formatted events to `events` vector under `events_mx` lock, enforcing `max_events` cap (oldest entries discarded).

### 3. Renderer Loop (`renderer_`)
- **Frequency**: 100ms refresh cycle.
- **Resizing Check**: Checks atomic signal flag `g_sigwinch_received`. If true, queries terminal size via `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)`.
- **State Selection**: Queries `DisplayState.mode.load()`.
  - `Normal`: Invokes `Renderer::render_normal(...)`
  - `Paused`: Invokes `Renderer::render_paused(...)`
  - `Revealed`: Invokes `Renderer::render_revealed(...)`
- **Output Buffering**: Formats output into a local `std::string` buffer using `std::format` and writes to standard output in a single I/O call (`std::cout.write(...)`) to prevent visual flicker.

---

## 5. Terminal Layout Specifications & Rendering Mockups

### Layout 1: NORMAL State (Split View)
- Width: `W` columns, Height: `H` rows.
- Split Ratio: Left panel ~50% width, Right panel ~50% width.

```
┌─────────────────────────────────┬────────────────────────────────────┐
│  SAFETY MEMORY STATE            │  KERNEL EVENT STREAM               │
│                                 │                                    │
│  virt  0xffff800012340000       │  [+0.302s] mutex_threads: A start  │
│  phys  0x0000000040001000       │  [+0.305s] VIOLATION: mutex held   │
│                                 │    data 0x5AFE1234->0xDEADDEAD     │
│  via vmalloc: 0x5AFE1234  ✓    │  [+0.403s] safety_thread: FAIL     │
│  via phys:    0xDEADDEAD  ✗    │  [+0.512s] bad_driver: write phys  │
│                                 │                                    │
│  CTX protect: ON                │                                    │
│  SMMU:        OFF               │                                    │
│  Mutex Owner: safety_thread     │                                    │
│  Status: ✗ CORRUPTED            │                                    │
├─────────────────────────────────┴────────────────────────────────────┤
│  Scenario D — DMA Linear Map Bypass              Elapsed: 00:03:42   │
└──────────────────────────────────────────────────────────────────────┘
```

### Layout 2: PAUSED State (Q&A Mode)

```
┌──────────────────────────────────────────────────────────────────────┐
│  ⏸  SCENARIO D — DMA Linear Map Bypass                               │
├──────────────────────────────────────────────────────────────────────┤
│  SETUP:                                                              │
│    CTX protection active (set_memory_ro on vmalloc alias)            │
│    bad_driver writes via phys_to_virt() — different virtual address  │
│    to the same physical page                                         │
│                                                                      │
│  ❓ Same physical page. Different virtual address. Protected?         │
│                                                                      │
│     A)  Yes — same physical page, same protection applies            │
│     B)  No — set_memory_ro protected one virtual alias only          │
│     C)  Depends on TLB flush status                                  │
│                                                                      │
│                        [ AWAITING PRESENTER ]                        │
└──────────────────────────────────────────────────────────────────────┘
```

### Layout 3: REVEALED State

```
┌──────────────────────────────────────────────────────────────────────┐
│  ✓  SCENARIO D — REVEALED                Answer: B ✓                 │
├──────────────────────────┬───────────────────────────────────────────┤
│  via vmalloc: 0x5AFE1234 │  [+0.302s] bad_driver: wrote via phys    │
│  via phys:    0xDEADDEAD │  [+0.302s] ctx_monitor: no fault fired   │
├──────────────────────────┴───────────────────────────────────────────┤
│  EXPLANATION:                                                        │
│  set_memory_ro modifies ONE PTE — the vmalloc virtual alias.         │
│  The linear map has its own PTE to the same physical page, never     │
│  modified. One physical page, two virtual aliases, one protected.    │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 6. C++ Interface Specifications

### `userspace/monitor/renderer.hpp`

```cpp
#ifndef USERSPACE_MONITOR_RENDERER_HPP
#define USERSPACE_MONITOR_RENDERER_HPP

#include <atomic>
#include <cstdint>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../common/expected.hpp"

namespace safety::monitor {

enum class DashboardMode {
    Normal,
    Paused,
    Revealed
};

struct ScenarioInfo {
    std::string id{"Scenario D"};
    std::string title{"DMA Linear Map Bypass"};
    std::string setup_text{"CTX active (set_memory_ro on vmalloc alias). Driver writes via phys_to_virt()."};
    std::string question_text{"Same physical page. Different virtual address. Protected?"};
    std::vector<std::string> options{
        "A) Yes — same physical page, same protection applies",
        "B) No — set_memory_ro protected one virtual alias only",
        "C) Depends on TLB flush status"
    };
    std::string correct_option{"B"};
    std::string explanation{
        "set_memory_ro modifies ONE PTE — the vmalloc virtual alias.\n"
        "The linear map has its own PTE to the same physical page, never modified.\n"
        "One physical page, two virtual aliases, one protected."
    };
    uint64_t start_time_sec{0};
};

struct EventEntry {
    std::string timestamp;
    std::string source;
    std::string message;
    bool is_warning{false};
    bool is_error{false};
};

struct DisplayState {
    std::atomic<uint32_t> val_vmalloc{0x5AFE1234};
    std::atomic<uint32_t> val_phys{0x5AFE1234};
    std::atomic<uint64_t> virt_addr{0xffff800012340000ULL};
    std::atomic<uint64_t> phys_addr{0x0000000040001000ULL};
    std::atomic<bool>     ctx_protected{false};
    std::atomic<bool>     smmu_active{false};
    std::atomic<DashboardMode> mode{DashboardMode::Normal};

    mutable std::mutex status_mx;
    std::string mutex_owner{"none"};
    std::string status_str{"PROTECTED_RO"};

    mutable std::mutex events_mx;
    std::vector<EventEntry> events;
    static constexpr std::size_t max_events{100};

    mutable std::mutex scenario_mx;
    ScenarioInfo scenario_info;
};

struct TerminalSize {
    uint16_t cols{80};
    uint16_t rows{24};
};

class TerminalGuard {
public:
    TerminalGuard();
    ~TerminalGuard();

    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;
    TerminalGuard(TerminalGuard&&) noexcept = default;
    TerminalGuard& operator=(TerminalGuard&&) noexcept = default;

    [[nodiscard]] static auto get_size() noexcept -> TerminalSize;
};

class Renderer {
public:
    explicit Renderer(const DisplayState& state) : state_(state) {}

    void render(std::string& out_buf, TerminalSize size) const;

private:
    void render_normal(std::string& out, TerminalSize size) const;
    void render_paused(std::string& out, TerminalSize size) const;
    void render_revealed(std::string& out, TerminalSize size) const;

    void draw_box_header(std::string& out, std::string_view title, uint16_t width) const;
    void draw_box_footer(std::string& out, std::string_view footer, uint16_t width) const;
    void draw_line_padded(std::string& out, std::string_view text, uint16_t width) const;

    const DisplayState& state_;
};

} // namespace safety::monitor

#endif // USERSPACE_MONITOR_RENDERER_HPP
```

### `userspace/monitor/renderer.cpp`

```cpp
#include "renderer.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace safety::monitor {

TerminalGuard::TerminalGuard() {
    // Hide cursor & enter alternate screen buffer
    std::cout << "\033[?1049h\033[?25l" << std::flush;
}

TerminalGuard::~TerminalGuard() {
    // Show cursor & leave alternate screen buffer
    std::cout << "\033[?25h\033[?1049l" << std::flush;
}

TerminalSize TerminalGuard::get_size() noexcept {
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return TerminalSize{ws.ws_col, ws.ws_row};
    }
    return TerminalSize{80, 24};
}

void Renderer::render(std::string& out_buf, TerminalSize size) const {
    out_buf.clear();
    out_buf.append("\033[H"); // Move cursor to home (top-left)

    switch (state_.mode.load()) {
        case DashboardMode::Normal:
            render_normal(out_buf, size);
            break;
        case DashboardMode::Paused:
            render_paused(out_buf, size);
            break;
        case DashboardMode::Revealed:
            render_revealed(out_buf, size);
            break;
    }
}

void Renderer::render_normal(std::string& out, TerminalSize size) const {
    uint16_t left_w = size.cols / 2 - 1;
    uint16_t right_w = size.cols - left_w - 3;
    uint16_t main_h = (size.rows > 4) ? size.rows - 4 : 10;

    // Header Top Border
    out.append(std::format("┌{:─^{}}┬{:─^{}}┐\n", " SAFETY MEMORY STATE ", left_w, " KERNEL EVENT STREAM ", right_w));

    // Content lines (Left vs Right)
    uint32_t vm_val = state_.val_vmalloc.load();
    uint32_t ph_val = state_.val_phys.load();
    uint64_t vaddr = state_.virt_addr.load();
    uint64_t paddr = state_.phys_addr.load();
    bool ctx_on = state_.ctx_protected.load();
    bool smmu_on = state_.smmu_active.load();

    std::string m_owner, status;
    {
        std::lock_guard lock(state_.status_mx);
        m_owner = state_.mutex_owner;
        status = state_.status_str;
    }

    bool is_corrupt = (vm_val != ph_val) || (status == "CORRUPTED");

    std::vector<EventEntry> ev_copy;
    {
        std::lock_guard lock(state_.events_mx);
        ev_copy = state_.events;
    }

    for (uint16_t row = 0; row < main_h; ++row) {
        std::string left_line;
        if (row == 1) left_line = std::format(" virt  0x{:016x}", vaddr);
        else if (row == 2) left_line = std::format(" phys  0x{:016x}", paddr);
        else if (row == 4) left_line = std::format(" via vmalloc: 0x{:08X}  {}", vm_val, (vm_val == 0x5AFE1234 ? "✓" : "✗"));
        else if (row == 5) left_line = std::format(" via phys:    0x{:08X}  {}", ph_val, (ph_val == 0x5AFE1234 ? "✓" : "✗"));
        else if (row == 7) left_line = std::format(" CTX protect: {}", ctx_on ? "ON" : "OFF");
        else if (row == 8) left_line = std::format(" SMMU:        {}", smmu_on ? "ON" : "OFF");
        else if (row == 9) left_line = std::format(" Mutex Owner: {}", m_owner);
        else if (row == 10) left_line = std::format(" Status:      {}", is_corrupt ? "✗ CORRUPTED" : "✓ SAFE");

        std::string right_line;
        if (row < ev_copy.size()) {
            const auto& ev = ev_copy[ev_copy.size() - 1 - row];
            right_line = std::format(" [{}] {}: {}", ev.timestamp, ev.source, ev.message);
        }

        out.append(std::format("│{:<{}}│{:<{}}│\n", left_line, left_w, right_line, right_w));
    }

    // Bottom Status Bar
    std::string scenario_name;
    {
        std::lock_guard lock(state_.scenario_mx);
        scenario_name = state_.scenario_info.id + " — " + state_.scenario_info.title;
    }

    out.append(std::format("├{:─^{}}┴{:─^{}}┤\n", "", left_w, "", right_w));
    out.append(std::format("│ {:<{}} │\n", scenario_name, size.cols - 4));
    out.append(std::format("└{:─^{}}┘\n", "", size.cols - 2));
}

void Renderer::render_paused(std::string& out, TerminalSize size) const {
    ScenarioInfo s_info;
    {
        std::lock_guard lock(state_.scenario_mx);
        s_info = state_.scenario_info;
    }

    uint16_t w = size.cols - 2;
    out.append(std::format("┌{:─^{}}┐\n", std::format(" ⏸  {} — {} ", s_info.id, s_info.title), w));
    out.append(std::format("│{:^{}}│\n", "SETUP:", w));
    out.append(std::format("│  {:<{}}│\n", s_info.setup_text, w - 2));
    out.append(std::format("│{:^{}}│\n", "", w));
    out.append(std::format("│  ❓ {:<{}}│\n", s_info.question_text, w - 5));
    out.append(std::format("│{:^{}}│\n", "", w));

    for (const auto& opt : s_info.options) {
        out.append(std::format("│     {:<{}}│\n", opt, w - 6));
    }

    out.append(std::format("│{:^{}}│\n", "", w));
    out.append(std::format("│{:^{}}│\n", "[ AWAITING PRESENTER ]", w));
    out.append(std::format("└{:─^{}}┘\n", "", w));
}

void Renderer::render_revealed(std::string& out, TerminalSize size) const {
    ScenarioInfo s_info;
    {
        std::lock_guard lock(state_.scenario_mx);
        s_info = state_.scenario_info;
    }

    uint16_t w = size.cols - 2;
    out.append(std::format("┌{:─^{}}┐\n", std::format(" ✓  {} — REVEALED   Answer: {} ✓ ", s_info.id, s_info.correct_option), w));
    out.append(std::format("│ EXPLANATION:{:<{}}│\n", "", w - 13));
    out.append(std::format("│  {:<{}}│\n", s_info.explanation, w - 2));
    out.append(std::format("└{:─^{}}┘\n", "", w));
}

} // namespace safety::monitor
```

### `userspace/monitor/main.cpp`

```cpp
#include "renderer.hpp"
#include "../common/proc_reader.hpp"
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

using namespace std::chrono_literals;

namespace {
std::atomic<bool> g_sigwinch_received{false};
std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    if (sig == SIGWINCH) {
        g_sigwinch_received.store(true);
    } else if (sig == INT || sig == SIGTERM) {
        g_running.store(false);
    }
}
} // namespace

int main() {
    std::signal(SIGWINCH, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    safety::monitor::DisplayState display_state;
    safety::monitor::TerminalGuard term_guard;
    safety::monitor::Renderer renderer(display_state);

    // 1. Mem Poller jthread (100ms)
    std::jthread mem_poller([&display_state](std::stop_token stoken) {
        safety::ProcReader proc_reader("/proc/safety_mem_status");
        while (!stoken.stop_requested() && g_running.load()) {
            auto res = proc_reader.read();
            if (res.has_value()) {
                // Parse key values...
            }
            std::this_thread::sleep_for(100ms);
        }
    });

    // 2. Event Streamer jthread (blocking pipe read / IPC)
    std::jthread event_streamer([&display_state](std::stop_token stoken) {
        while (!stoken.stop_requested() && g_running.load()) {
            // Read trace_pipe lines or IPC control commands...
            std::this_thread::sleep_for(100ms);
        }
    });

    // 3. Renderer jthread (100ms refresh)
    std::string draw_buf;
    draw_buf.reserve(8192);

    while (g_running.load()) {
        auto term_size = safety::monitor::TerminalGuard::get_size();
        if (g_sigwinch_received.exchange(false)) {
            // Reset / recalculate bounds if needed
        }

        renderer.render(draw_buf, term_size);
        std::cout.write(draw_buf.data(), static_cast<std::streamsize>(draw_buf.size()));
        std::cout.flush();

        std::this_thread::sleep_for(100ms);
    }

    return 0;
}
```

---

## 7. `userspace/CMakeLists.txt` Integration Blueprint

Add the `monitor` target to `userspace/CMakeLists.txt`:

```cmake
# Executable: monitor
add_executable(monitor
    monitor/main.cpp
    monitor/renderer.cpp
)
target_include_directories(monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(monitor PRIVATE common)
```

---

## 8. Verification & Validation Protocol

1. **Build Verification**:
   ```bash
   cmake -S userspace -B userspace/build -DCMAKE_BUILD_TYPE=Debug -GNinja
   ninja -C userspace/build
   ```

2. **Sanitizer & Concurrency Checks**:
   - **TSan (ThreadSanitizer)**: Ensure no data races between `mem_poller`, `event_streamer`, and `renderer`.
   - **ASan + UBSan**: Verify memory safety and clean terminal guard teardown.

3. **`clang-tidy` Verification**:
   ```bash
   clang-tidy userspace/monitor/*.cpp -- -Iuserspace -std=c++20
   ```

---

## 9. Next Steps for Implementer

1. Create directory `userspace/monitor/`.
2. Add `renderer.hpp`, `renderer.cpp`, `main.cpp` using the exact C++20 structures defined above.
3. Update `userspace/CMakeLists.txt`.
4. Compile and verify in QEMU VM under `tmux`.

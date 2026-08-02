# Handoff Report — TUI Monitor Dashboard Blueprint

**Working Directory**: `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_1`  
**Target Output**: `userspace/monitor/` (`main.cpp`, `renderer.hpp`, `renderer.cpp`) and `userspace/CMakeLists.txt`  
**Blueprint Location**: `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_1/analysis.md`  

---

## 1. Observation

1. **Kernel Procurement Interfaces**:
   - `/proc/safety_mem_status` (`kernel/safety_mem/safety_mem.c:223-258`) exports lines: `virt_addr`, `phys_addr`, `value_via_vmalloc`, `value_via_phys`, `ctx_protected`, `smmu_active`, `mutex_owner`, `status`.
   - `/sys/kernel/tracing/trace_pipe` (or `/sys/kernel/debug/tracing/trace_pipe`) streaming kernel events via `trace_printk`.
   - Control signals/markers can be written by `harness` into `/sys/kernel/tracing/trace_marker` to trigger state transitions (`PAUSED`, `REVEALED`, `NORMAL`).

2. **Existing Userspace Infrastructure**:
   - `userspace/common/proc_reader.hpp`: Provides RAII-based file reader returning `std::expected<std::string, std::string>`.
   - `userspace/common/expected.hpp`: C++20 `std::expected` / fallback standard interface.
   - `userspace/CMakeLists.txt`: Configured for C++23 standard with `-Wall -Wextra -Werror -Wpedantic`. Target `monitor` needs to be added.

3. **Core Architectural Requirements**:
   - 3x `std::jthread` concurrent loops (`mem_poller`, `event_streamer`, `renderer`).
   - Standard C++ Core Guidelines: CP.25 (prefer `jthread`), CP.20 (RAII locks), R.1 (RAII resource management), I.11/I.13 (no raw owning pointers/arrays), E.1 (`std::expected`), F.15 (`std::string_view`).
   - Dynamic terminal geometry recalculation on `SIGWINCH` via `ioctl(TIOCGWINSZ)`.
   - Three TUI rendering states: NORMAL (split view), PAUSED (Q&A mode), REVEALED (answer & explanation split).

---

## 2. Logic Chain

1. **Observation**: `mem_poller` must query kernel status every 100ms without blocking terminal redrawing or kernel trace event reading.
   - **Deduction**: Isolate `mem_poller` into a dedicated `std::jthread` running a 100ms loop using `ProcReader("/proc/safety_mem_status")` and updating atomic/mutex-protected fields in `DisplayState`.

2. **Observation**: Kernel events and `harness` state control commands stream continuously from `trace_pipe`.
   - **Deduction**: Isolate `event_streamer` into a dedicated `std::jthread` performing blocking pipe/file reads, parsing line-by-line event strings and IPC control markers (`DEMO_CTRL:`), and appending log entries to a thread-safe ring buffer (`events` vector with cap 100).

3. **Observation**: The terminal renderer must redraw every 100ms with zero visual tearing and clean restoration on exit.
   - **Deduction**: Implement `renderer` in a 3rd `std::jthread` (or main loop) that formats the entire frame into a single std::string buffer using `std::format` and writes it atomically to `std::cout`. Use RAII class `TerminalGuard` to manage cursor hiding (`\033[?25l`), showing (`\033[?25h`), and alternate screen buffer (`\033[?1049h/l`).

4. **Observation**: Screen size can change dynamically during live presentations (e.g. window resizing or resolution changes).
   - **Deduction**: Register a signal handler for `SIGWINCH` setting an atomic boolean flag (`g_sigwinch_received`). When set, `renderer` calls `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` to dynamically update layout split geometry (`left_w`, `right_w`, `main_h`).

---

## 3. Caveats

- **Missing Kernel Modules at Startup**: When `monitor` starts, `/proc/safety_mem_status` or `trace_pipe` may not be available until `harness` loads the kernel modules. The implementation handles missing files gracefully by displaying `MODULE_NOT_LOADED` instead of crashing.
- **Tracefs Permissions**: Reading `trace_pipe` requires root privileges in QEMU VM environment (`root`).
- **Terminal Constraints**: If terminal dimensions are smaller than 60x15, layout gracefully clamps borders to prevent buffer overflow or string formatting exceptions.

---

## 4. Conclusion

A comprehensive implementation blueprint has been created and documented in `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_1/analysis.md`. The design is fully compliant with C++ Core Guidelines and ready for implementer execution.

---

## 5. Verification Method

To verify the implementation once written by the implementer:

1. **Build Verification**:
   ```bash
   cd /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -GNinja
   ninja -C build
   ```
2. **Concurrency & Race Condition Verification (TSan)**:
   ```bash
   cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread" -GNinja
   ninja -C build-tsan
   ./build-tsan/bin/monitor
   ```
3. **Static Analysis Compliance**:
   ```bash
   clang-tidy userspace/monitor/*.cpp -- -Iuserspace -std=c++20
   ```
4. **Execution in QEMU**:
   - Run `./env/run_qemu.sh`
   - Execute `monitor` in left pane of `tmux` while running `harness --interactive` in right pane.

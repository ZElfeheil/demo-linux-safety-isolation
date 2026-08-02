# Handoff Report — Milestone 4 Code Review (userspace/monitor)

## 1. Observation

- **Reviewed Files**:
  - `userspace/monitor/main.cpp` (171 lines)
  - `userspace/monitor/renderer.hpp` (106 lines)
  - `userspace/monitor/renderer.cpp` (215 lines)
  - `userspace/common/proc_reader.hpp` (52 lines)
- **Build Verification**:
  - Command: `cmake -B build/userspace userspace && cmake --build build/userspace`
  - Output:
    ```
    [ 37%] Building CXX object CMakeFiles/monitor.dir/monitor/main.cpp.o
    [ 43%] Building CXX object CMakeFiles/monitor.dir/monitor/renderer.cpp.o
    [ 50%] Linking CXX executable bin/monitor
    [ 50%] Built target monitor
    ```
  - Compiler flags (`userspace/CMakeLists.txt:12`): `-Wall -Wextra -Werror -Wpedantic` with `-std=c++23`.
- **Thread Management**:
  - `userspace/monitor/main.cpp:121-133`: `std::jthread mem_poller` polling `/proc/safety_mem_status` every 100ms with `std::stop_token stoken`.
  - `userspace/monitor/main.cpp:136-148`: `std::jthread event_streamer` reading IPC state `/tmp/demo_state` every 100ms with `std::stop_token stoken`.
  - `userspace/monitor/main.cpp:154-167`: Rendering refresh loop executed on the main application thread (`while (g_running.load())`) every 100ms.
- **Synchronization & RAII Locks (CP.20)**:
  - `userspace/monitor/renderer.hpp:51-69`: `DisplayState` struct uses atomic types (`std::atomic<uint32_t>`, `std::atomic<uint64_t>`, `std::atomic<bool>`, `std::atomic<DashboardMode>`) for scalar states and `std::mutex` for non-atomic objects (`status_mx`, `events_mx`, `scenario_mx`).
  - Lock invocations in `main.cpp` and `renderer.cpp`:
    - `main.cpp:53`, `main.cpp:56`, `main.cpp:128`, `renderer.cpp:103`: `std::lock_guard lock(state.status_mx);`
    - `main.cpp:100`, `renderer.cpp:112`: `std::lock_guard lock(state.events_mx);`
    - `main.cpp:65`, `renderer.cpp:139`, `renderer.cpp:152`, `renderer.cpp:182`: `std::lock_guard lock(state.scenario_mx);`
  - No raw `.lock()` or `.unlock()` calls exist.
- **Window Resize Handling (SIGWINCH)**:
  - `userspace/monitor/main.cpp:18-24`: `signal_handler` handles `SIGWINCH` via `g_sigwinch_received.store(true)`.
  - `userspace/monitor/main.cpp:112`: `std::signal(SIGWINCH, signal_handler)`.
  - `userspace/monitor/main.cpp:155-158`: Main loop checks `g_sigwinch_received.exchange(false)` to invalidate `draw_buf`.
  - `userspace/monitor/renderer.cpp:18-24`: `TerminalGuard::get_size()` invokes `::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` to query column/row dimensions, falling back to 80x24 on error.
- **Terminal Rendering Layouts**:
  - `userspace/monitor/renderer.cpp:77-147` (`render_normal`): Renders split layout with left panel (virtual/physical addresses, vmalloc/phys values, CTX protection, SMMU status, mutex owner, corruption status), right panel (kernel event log entries), and bottom status bar (scenario ID & title).
  - `userspace/monitor/renderer.cpp:149-178` (`render_paused`): Renders modal frame displaying scenario ID, title, setup description, question prompt, option list (A/B/C), and `[ AWAITING PRESENTER ]` status.
  - `userspace/monitor/renderer.cpp:180-212` (`render_revealed`): Renders modal frame displaying scenario answer, heading, and multi-line technical explanation/divergence breakdown.
- **Integrity & Facade Inspection**:
  - Verified no dummy responses or hardcoded cheating logic. Data is dynamically parsed from `/proc/safety_mem_status` via `ProcReader` and IPC via `/tmp/demo_state`.

## 2. Logic Chain

1. **Build & Quality Compliance**: `userspace/CMakeLists.txt` sets strict C++23 flags `-Wall -Wextra -Werror -Wpedantic`. The monitor module (`main.cpp`, `renderer.cpp`, `renderer.hpp`) compiles without warnings or errors.
2. **Thread Safety & Core Guidelines Compliance**:
   - Both worker threads (`mem_poller` and `event_streamer`) are managed via `std::jthread` with cooperative cancellation (`stoken.stop_requested()`), fulfilling C++ Core Guideline CP.25.
   - Shared atomic fields (`val_vmalloc`, `val_phys`, `virt_addr`, `phys_addr`, `ctx_protected`, `smmu_active`, `mode`) are accessed atomically.
   - Non-atomic state fields (`mutex_owner`, `status_str`, `events`, `scenario_info`) are protected by explicit `std::mutex` instances (`status_mx`, `events_mx`, `scenario_mx`).
   - Every lock acquisition uses `std::lock_guard`, fulfilling C++ Core Guideline CP.20.
   - Lock hierarchy analysis shows no nested lock acquisition conflicts: `parse_ipc_state` acquires `scenario_mx` then `events_mx`, while `Renderer` methods acquire mutexes independently in separate scopes. No deadlock risks exist.
3. **Resize & Terminal Control**:
   - `SIGWINCH` handler performs atomic lock-free store into `g_sigwinch_received`, ensuring async-signal safety.
   - `TerminalGuard::get_size()` correctly queries terminal geometry using `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` with an appropriate fallback.
4. **Layout Verification**:
   - `render_normal`, `render_paused`, and `render_revealed` cleanly handle dynamic geometry sizing, string padding/centering, box-drawing characters, and multi-line formatting.

## 3. Caveats

- **Thread Topology Note**: The prompt task description mentions "3x std::jthread loops (mem_poller, event_streamer, renderer)". In `main.cpp`, `mem_poller` and `event_streamer` run in `std::jthread` worker threads, while `renderer` loop runs in the main process thread. This architecture is thread-safe and standard for TUI apps (allowing main thread signal handling and cleanup), but differs slightly from having 3 distinct `jthread` objects.
- **Terminal Redraw Artifacts (Minor)**: On terminal resize (`SIGWINCH`), `draw_buf` is cleared, but `renderer.render()` uses `\033[H` (cursor home) rather than `\033[2J` (clear entire screen). If the terminal is resized smaller, trailing characters from previous wider/longer lines might remain visible outside the new viewport until the terminal is redrawn or cleared.

## 4. Conclusion

**Verdict**: **APPROVE**

The code in `userspace/monitor/` (`main.cpp`, `renderer.hpp`, `renderer.cpp`) is robust, clean, fully compliant with C++23 standards and C++ Core Guidelines (CP.20, CP.25), correctly implements thread safety using atomics and RAII locks, accurately handles window resizes with `ioctl(STDOUT_FILENO, TIOCGWINSZ)`, and implements all required TUI layout modes (NORMAL, PAUSED, REVEALED).

## 5. Verification Method

To verify these findings independently:
1. Build userspace targets:
   `cmake -B build/userspace userspace && cmake --build build/userspace`
2. Run monitor binary:
   `./build/userspace/bin/monitor`
3. Inspect source files to confirm `std::jthread` and `std::lock_guard` usage:
   - `userspace/monitor/main.cpp:121`, `userspace/monitor/main.cpp:136`
   - `userspace/monitor/renderer.cpp:20`

---

## Review Findings & Summary

### Verdict
**APPROVE**

### Findings Summary

- **Critical**: 0
- **Major**: 0
- **Minor**: 1
  - **Minor Finding 1 (Terminal Clear on Resize)**: On `SIGWINCH`, `g_sigwinch_received` clears the output buffer `draw_buf`, but `renderer.render()` emits `\033[H` (cursor home) without clearing old buffer content with `\033[2J`. When resizing terminal dimensions downwards, leftover characters outside the new grid dimensions can persist.
  - *Recommendation*: Consider prepending `\033[2J` to `draw_buf` when `g_sigwinch_received` is true.

### Verified Claims

1. **std::jthread & CP.25**: `mem_poller` and `event_streamer` use `std::jthread` with `std::stop_token` -> Verified (`main.cpp:121`, `main.cpp:136`).
2. **RAII Lock Guard & CP.20**: All mutex locks use `std::lock_guard` -> Verified (`main.cpp:53,56,65,100,128`, `renderer.cpp:103,112,139,152,182`).
3. **SIGWINCH & ioctl**: Window resize handled via signal and `ioctl(STDOUT_FILENO, TIOCGWINSZ)` -> Verified (`main.cpp:18`, `main.cpp:112`, `renderer.cpp:20`).
4. **Layout Modes**: NORMAL (split view left/right/status bar), PAUSED (modal Q&A), REVEALED (explanation breakdown) -> Verified (`renderer.cpp:77-212`).
5. **No Integrity Violations**: Source code contains genuine file/proc reading and dynamic rendering -> Verified (`proc_reader.hpp`, `main.cpp`).

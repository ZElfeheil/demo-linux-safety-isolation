## 2026-07-31T05:33:29Z
You are assigned to implement Milestone 4 (TUI Dashboard & Presenter Harness System) for the ARM64 Linux 6.6 Safety Isolation Demonstration System.

Working Directory for metadata: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m4
Project Root: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Your Tasks:
1. Initialize your briefing / progress files in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m4.
2. Refer to the implementation blueprints created by explorers:
   - /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_1/analysis.md
   - /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_2/analysis.md
   - docs/implementation_plan.md
3. Implement the C++20 TUI monitor & harness codebase:
   - `userspace/monitor/`: `renderer.hpp`, `renderer.cpp`, `main.cpp`
     - 3x `std::jthread` (mem_poller, event_streamer, renderer).
     - Display states: Normal (split view), Paused (Q&A choice frame + AWAITING PRESENTER banner), Revealed (answer & explanation frame).
     - `SIGWINCH` signal handling with dynamic `ioctl(TIOCGWINSZ)` recalculation.
     - Single-buffer `std::cout.write` output to eliminate flicker.
   - `userspace/harness/`:
     - `module_loader.hpp`, `module_loader.cpp`: RAII kernel module loader (`insmod`/`rmmod`) with `std::expected` and signal handlers (`SIGINT`/`SIGTERM`) for safe module cleanup.
     - `interactive.hpp`, `interactive.cpp`: 4-beat presenter engine (Setup, Question, Reveal, Explain), `TermiosGuard` for raw non-canonical single-keypress, and `ensure_tmux_environment()` auto-spawner (left pane: `monitor`, right pane: `harness`).
     - `scenarios/`: `scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, `scenario_g.cpp`.
     - `main.cpp`: CLI arguments (`--interactive`, `--auto`, `--scenario <id|all>`, `--start-at <id>`).
   - `userspace/CMakeLists.txt`: Add target executables `monitor` and `harness` linking `common`, built with `-Wall -Wextra -Werror -Wpedantic`.
4. Perform build verification:
   - Run cross-compilation build using `cmake -S userspace -B userspace/build -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake -GNinja` and `ninja -C userspace/build` or `docker compose run build`.
   - Verify that `userspace/build/bin/monitor` and `userspace/build/bin/harness` compile cleanly with zero warnings/errors.
5. Create handoff.md in /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m4/ with verification results, command outputs, and summary of changes.
6. Send a message to orchestrator with handoff path when complete.

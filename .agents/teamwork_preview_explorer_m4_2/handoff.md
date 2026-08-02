# Milestone 4 Explorer Handoff Report: Presenter Harness & Scenarios

## 1. Observation

Direct code observations from repository analysis:

1. **`userspace/common/scenario.hpp` (Lines 31-38)**:
   ```cpp
   template<typename T>
   concept Scenario = requires(T s) {
       { s.name() } -> std::convertible_to<std::string_view>;
       { s.setup() } -> std::same_as<std::expected<void, std::string>>;
       { s.run() } -> std::same_as<ScenarioResult>;
       { s.teardown() } -> std::same_as<void>;
   };
   ```
   All scenario classes (`ScenarioB`, `ScenarioD`, `ScenarioF`, `ScenarioG`) must satisfy this concept.

2. **`userspace/common/proc_reader.hpp` (Lines 18-39)**:
   Provides `ProcReader` class using `std::expected<std::string, std::string>` to safely read procfs entries (`/proc/safety_mem_status`, `/proc/ctx_monitor_log`, `/proc/smmu_guard_log`).

3. **Kernel Interfaces**:
   - `kernel/safety_mem/safety_mem.c` (Lines 278-283): Writing `"1"` or `"protect"` calls `safety_mem_set_protection(true)`. Reading returns `value_via_vmalloc`, `value_via_phys`, `ctx_protected`, `smmu_active`, and `mutex_owner`.
   - `kernel/bad_driver/bad_driver.c` (Lines 146-152): Writing `"1"`, `"2"`, or `"3"` triggers attack modes 1 (vmalloc write attempt), 2 (unsynchronized write), or 3 (linear map `phys_to_virt` write bypass).
   - `kernel/mutex_threads/rogue_thread.c` (Lines 25-28): Module parameter `attack_mode=0` (unsynchronized write), `attack_mode=1` (lock metadata attack corrupting `mutex.owner`).

4. **`docs/implementation_plan.md` Requirements (Lines 372-417, 692-701)**:
   - File structure: `main.cpp`, `interactive.hpp`, `interactive.cpp`, `module_loader.hpp`, `module_loader.cpp`, `scenarios/` (`scenario_b.cpp`, `scenario_d.cpp`, `scenario_f.cpp`, `scenario_g.cpp`).
   - 4-beat presenter flow: Beat 1 SETUP, Beat 2 QUESTION (`set_paused(true)`, await single keypress), Beat 3 REVEAL (`set_paused(false)`, trigger bad driver write), Beat 4 EXPLAIN.
   - CLI flags: `--interactive`, `--auto`, `--scenario <id>`, `--start-at <id>`.
   - One-command tmux startup launching monitor in left pane and harness in right pane.
   - `ModuleLoader` RAII wrapper with signal handlers (`SIGINT`/`SIGTERM`) to clean up kernel modules on exit.

5. **Blueprint File Saved**:
   Detailed blueprint written to `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_2/analysis.md`.

---

## 2. Logic Chain

1. **Requirement Alignment**:
   - `docs/implementation_plan.md` mandates an interactive TUI presenter harness to drive live 15-minute presentations.
   - The harness must present 3 core scenarios (B, D, F) and 1 optional scenario (G).

2. **4-Beat Presenter State Machine**:
   - Beat 1 (SETUP) initializes module state via `ModuleLoader` and notifies monitor via `/tmp/demo_state`.
   - Beat 2 (QUESTION) sets monitor state to `PAUSED`, renders the multiple-choice question on screen, and switches terminal stdin to non-canonical, no-echo mode (`termios`) so presenter can advance with any keypress.
   - Beat 3 (REVEAL) sets monitor state to `REVEALED`, writes attack commands to `/proc/bad_driver_ts` or loads `rogue_thread.ko`, allowing monitor to render live data divergence or exception traps.
   - Beat 4 (EXPLAIN) highlights the correct choice, outputs isolation engineering rationale, and calls scenario `teardown()` to unload modules cleanly.

3. **ModuleLoader Lifecycle & VM Safety**:
   - Loading modules into Linux kernel requires strict ordering and guaranteed cleanup on premature exit (Ctrl+C / `SIGINT` / `SIGTERM`).
   - `ModuleLoader` maintains a stack of loaded module names. On destruction or signal reception, `unload_all()` executes `rmmod` in reverse order of insertion, ensuring QEMU VM kernel state remains clean.

4. **Tmux Auto-Launch Strategy**:
   - `PresenterEngine::ensure_tmux_environment()` checks `$TMUX`. If empty, spawns `tmux new-session -d -s demo` with split windows (`monitor` left pane 0.0, `harness` right pane 0.1) and attaches, giving a seamless single-command launcher experience (`harness --interactive`).

---

## 3. Caveats

- **Root Privilege**: `insmod` / `rmmod` operations require root privilege inside the target QEMU VM filesystem.
- **Kernel Module Path**: Scenario setup assumes `.ko` files are located in `/lib/modules/` or current execution working directory inside rootfs.
- **`tmux` Installation**: Auto-launch expects `tmux` to be available on `PATH` in the target system.

---

## 4. Conclusion

The implementation blueprint for `userspace/harness/` is complete, fully specified, and saved to `analysis.md`. It provides exact C++ class layouts, header declarations, implementation source files, 4-beat presenter flow mechanics, signal-safe `ModuleLoader` RAII wrapper, CLI flag parsing, and CMake build configuration. It is ready for immediate implementation by Worker agents.

---

## 5. Verification Method

To independently verify the blueprint and upcoming implementation:

1. **Inspect Blueprint Artifact**:
   ```bash
   cat /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m4_2/analysis.md
   ```
2. **Build Harness Target (After Worker implementation)**:
   ```bash
   cmake -B build -S userspace -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake
   cmake --build build --target harness
   ```
3. **Run Automated Scenario Suite**:
   ```bash
   ./bin/harness --auto --scenario all
   ```
4. **Verify Signal Handler Module Teardown**:
   - Execute `./bin/harness --interactive --scenario B`
   - Interrupt with Ctrl+C during Question Beat.
   - Run `lsmod` inside QEMU VM to confirm all modules (`safety_mem`, `mutex_threads`, `rogue_thread`) are cleanly unloaded.

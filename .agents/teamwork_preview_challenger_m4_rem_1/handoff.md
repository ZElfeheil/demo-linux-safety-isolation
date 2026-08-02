# Handoff Report — Empirical Adversarial Challenge: Milestone 4 TUI Monitor

**Target Module**: `userspace/monitor/` (`renderer.hpp`, `renderer.cpp`, `main.cpp`)  
**Working Directory**: `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_challenger_m4_rem_1`  
**Date**: 2026-07-31  

---

## 1. Observation

Empirical testing and adversarial static/dynamic analysis of the TUI Monitor Dashboard yielded the following direct observations:

### Observation A: Concurrency Safety & ThreadSanitizer (TSAN)
- **Command Executed**: `clang++ -std=c++20 -fsanitize=thread -g -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_concurrency.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_concurrency_tsan && .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_concurrency_tsan`
- **Output**:
  ```text
  [STRESS TEST] Starting Concurrency Safety Test...
  [STRESS TEST PASS] Completed without crash or hang!
    Renders completed: 6446
    Poller updates:    12642
    Streamer updates:  4817
  ```
- **Finding**: Zero C++ data races or memory safety bugs detected under TSAN. `DisplayState` mutexes (`status_mx`, `events_mx`, `scenario_mx`) properly synchronize non-atomic string and vector data structures.

### Observation B: Empirical Snapshot Tearing & False Corruption Alarms
- **Code Locations**: `userspace/monitor/main.cpp:83-85` and `userspace/monitor/renderer.cpp:111-125`
- **Command Executed**: `clang++ -std=c++20 -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_tearing.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_tearing && .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_tearing`
- **Output**:
  ```text
  [EMPIRICAL TEST] Checking for snapshot tearing between mem_poller and renderer...
  Total checks: 60638
  Tearing / False Positive Corruptions detected: 790
  [FINDING CONFIRMED] Snapshot tearing bug reproduced! Renderer read uncoordinated atomics causing false corruption status!
  ```
- **Finding**: Out of 60,638 snapshot reads during concurrent poller updates, **790 checks (1.3%)** read torn atomic state where `val_vmalloc` was updated for cycle N+1 while `val_phys` was still at cycle N. This caused `is_corrupt = (vm_val != ph_val)` to evaluate to `true`, reporting false memory corruption.

### Observation C: Terminal Geometry & Box Deformation
- **Code Location**: `userspace/monitor/renderer.cpp:92-107`
- **Command Executed**: `clang++ -std=c++20 -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_geometry.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_geometry && .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_geometry`
- **Output Excerpts**:
  ```text
  --- Testing mode: Normal (40x12) | Terminal size: 40x12 ---
  Sample Line 0: ┌ SAFETY MEMORY STATE ┬ KERNEL EVENT STREAM ┐ [vlen=45]
  Sample Line 1: │ virt  0xffff80001234│                     │ [vlen=40]

  --- Testing mode: Normal (20x10 - Small) | Terminal size: 20x10 ---
  Total lines output: 12 (Requested height: 10)
  [FAIL/BUG] HEIGHT OVERFLOW! Output 12 lines, but terminal height is 10! (Will cause scrolling & flicker)
  [FAIL/BUG] LINE LENGTH MISMATCH! Line 1 has visual len 40 (Line 0 has 45)

  --- Testing mode: Paused (40x12) | Terminal size: 40x12 ---
  Sample Line 0: ┌ ⏸  Scenario D — DMA Linear Map Bypass ┐ [vlen=41]
  Sample Line 1: │ SETUP:                               │ [vlen=40]
  ```
- **Findings**:
  1. At 40 cols in `Normal` mode, the top header line visual width is **45**, while body lines are **40**. The titles `" SAFETY MEMORY STATE "` and `" KERNEL EVENT STREAM "` are not truncated, deforming the TUI box and misaligning vertical separator `┬` with `│`.
  2. For terminal heights < 12 (e.g. `20x10`), `render_normal` outputs **12 lines**, exceeding terminal height and forcing 2 lines of vertical scrolling every frame (10 Hz).
  3. `visual_len()` counts UTF-8 code points, treating 2-column wide emoji `⏸` (U+23F8) as width 1, causing top header padding in `Paused` mode to be 1 space too wide.

### Observation D: SIGWINCH Signal Responsiveness
- **Command Executed**: `clang++ -std=c++20 -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_sigwinch.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_sigwinch && .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_sigwinch`
- **Output**:
  ```text
  [STRESS TEST] Testing SIGWINCH and SIGINT/SIGTERM responsiveness...
    Sending SIGINT to self...
    Worker processed 402 frames.
  [STRESS TEST PASS] Process handled SIGWINCH (500 signals) and shutdown cleanly!
  ```
- **Finding**: Process handles rapid SIGWINCH signals (500 signals/sec) and SIGINT/SIGTERM shutdown without crashes, signal safety violations, or hangs.

---

## 2. Logic Chain

1. **Snapshot Tearing Logic Chain**:
   - `DisplayState` exposes `val_vmalloc` and `val_phys` as separate `std::atomic<uint32_t>` variables.
   - `mem_poller` parses `/proc/safety_mem_status` line-by-line in a loop. It updates `val_vmalloc.store(v1)` first, and `val_phys.store(v2)` in a subsequent loop iteration.
   - `Renderer::render_normal` reads `vm_val = val_vmalloc.load()` and `ph_val = val_phys.load()` sequentially without locking a snapshot mutex.
   - If the renderer reads `vm_val` after the poller updated it, but reads `ph_val` before the poller updated it, `vm_val != ph_val` is `true`.
   - Empirically, 1.3% of concurrent reads observed this torn state, triggering false positive `CORRUPTED` status reports on the TUI dashboard.

2. **Terminal Geometry Deformation Logic Chain**:
   - In `Renderer::render_normal`, `left_w` and `right_w` are calculated based on `cols` (min 40). At 40 cols, `left_w = 19` and `right_w = 18`.
   - `l_fill` is calculated as `(left_w > visual_len(left_title)) ? ... : 0`. Since `left_title` visual length is 23, `19 > 23` is false, making `l_fill = 0`.
   - `top_hdr` appends `"┌" + left_title + "┬" + right_title + "┐"`. Since `left_title` (23 chars) is not truncated, the header section visual width becomes `1 + 23 + 1 + 23 + 1 = 49` characters (or 45 for left+right fills), whereas body lines are fixed at `1 + 19 + 1 + 18 + 1 = 40` characters.
   - This creates a 5-character misalignment between header border and body border, deforming the box layout.

3. **Small Terminal Height Scrolling Logic Chain**:
   - `render_normal` clamps `rows = (size.rows < 12) ? 12 : size.rows`.
   - `render_normal` generates 1 header line + `rows - 4` body lines + 1 divider + 1 status line + 1 bottom border = 12 total lines.
   - If actual terminal height is 10 rows (e.g. 20x10 geometry), outputting 12 lines forces the terminal emulator to scroll down by 2 rows on every frame refresh (100ms cycle).
   - This leads to continuous visual jitter and destroys scrolling history.

---

## 3. Caveats

- **Kernel Trace Pipe Behavior**: Tests were conducted using synthetic trace streams and FIFOs. On real Linux kernels with active `trace_pipe`, `std::getline` behavior depends on whether trace entries are continuously produced by kernel modules.
- **Terminal Emulator Differences**: Unicode character width rendering (e.g. `⏸` U+23F8) varies slightly between standard VT100 terminals (which display `⏸` as 2 columns) and basic ASCII terminals.

---

## 4. Conclusion

**Overall Risk Assessment**: **MEDIUM**

The Milestone 4 TUI Monitor Dashboard (`userspace/monitor/`) demonstrates solid C++ thread safety, zero data races under ThreadSanitizer, and resilient signal handling under SIGWINCH hammering.

However, empirical stress testing revealed 4 functional/rendering defects:
1. **Snapshot Tearing (Severity: Medium)**: Lack of a single snapshot lock during `mem_poller` reading causes 1.3% false positive memory corruption states.
2. **TUI Box Deformation (Severity: Medium)**: Untruncated title strings in `render_normal` break box border alignment on terminal widths < 50 columns.
3. **Small Terminal Scrolling (Severity: Medium)**: Hardcoded minimum height clamp (12 lines) causes continuous vertical terminal scrolling when terminal height is < 12 rows.
4. **Emoji Width Offset (Severity: Low)**: `visual_len()` counts code points instead of display column width, causing 1-space header border overruns for double-width emojis (`⏸`).

**Recommended Mitigations**:
- Wrap `DisplayState` snapshot reads/writes in `mem_poller` and `Renderer` inside a single state snapshot mutex or atomic struct.
- Truncate `left_title` and `right_title` in `render_normal` if `visual_len(title) > width`.
- Respect actual terminal row limits (`size.rows`) and adapt body line count dynamically when `size.rows < 12`.

---

## 5. Verification Method

To independently verify these empirical findings:

1. **Verify Concurrency & Data Race Safety**:
   ```bash
   clang++ -std=c++20 -fsanitize=thread -g -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_concurrency.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_concurrency_tsan
   ./.agents/teamwork_preview_challenger_m4_rem_1/scratch/test_concurrency_tsan
   ```
   *Expected Result*: Process runs to completion with 0 TSAN warnings.

2. **Verify Snapshot Tearing & False Corruption Alarms**:
   ```bash
   clang++ -std=c++20 -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_tearing.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_tearing
   ./.agents/teamwork_preview_challenger_m4_rem_1/scratch/test_tearing
   ```
   *Expected Result*: Output reports >0 false positive corruptions (reproduced 790/60,638 checks).

3. **Verify Terminal Geometry & Box Deformation**:
   ```bash
   clang++ -std=c++20 -Iuserspace userspace/monitor/renderer.cpp .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_geometry.cpp -o .agents/teamwork_preview_challenger_m4_rem_1/scratch/test_geometry
   ./.agents/teamwork_preview_challenger_m4_rem_1/scratch/test_geometry
   ```
   *Expected Result*: Output demonstrates line length mismatches (vlen 45 vs 40) at 40x12, and 12-line height overflow at 20x10.

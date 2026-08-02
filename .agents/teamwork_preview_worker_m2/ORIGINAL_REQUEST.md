## 2026-07-30T19:43:00Z
You are a Worker agent.
Your Working Directory: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m2

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Task: Implement all Kernel Modules for Milestone 2 of the ARM64 Linux 6.6 Safety Isolation Demonstration System.

Project Root: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation

Reference Blueprints (Read these files for complete code specifications):
1. /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m2_1/handoff.md (safety_mem.c & bad_driver.c)
2. /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m2_2/handoff.md (mutex_threads.c & rogue_thread.c)
3. /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_explorer_m2_3/handoff.md (ctx_monitor.c, smmu_guard.c, & Makefiles)

Files to Create/Implement:
1. `kernel/Makefile` and sub-directory Makefiles (`kernel/safety_mem/Makefile`, `kernel/bad_driver/Makefile`, `kernel/mutex_threads/Makefile`, `kernel/ctx_monitor/Makefile`, `kernel/smmu_guard/Makefile`).
2. `kernel/safety_mem/safety_mem.h` and `kernel/safety_mem/safety_mem.c`
   - Memory allocation, PTE walk, PMD block splitting logic, `set_memory_ro`/`rw`, ARM64 memory barriers (`dsb(sy)` / `isb()`), `/proc/safety_mem_status` interface, GPL symbol exports.
3. `kernel/bad_driver/bad_driver.c`
   - Attack Mode 1 (vmalloc write attempt), Attack Mode 2 (mutex bypass unsynchronized write), Attack Mode 3 (phys_to_virt linear mapping bypass attempt), `/proc/bad_driver_ts` interface.
4. `kernel/mutex_threads/mutex_threads.h`, `kernel/mutex_threads/mutex_threads.c`, and `kernel/mutex_threads/rogue_thread.c`
   - Thread A (Safety thread), Thread B (Cooperative thread), Thread C (Rogue thread), `kthread_run`/`kthread_stop` lifecycle, ftrace event logging, mutex contention status interface.
5. `kernel/ctx_monitor/ctx_monitor.c`
   - `register_die_notifier` at `INT_MAX` for `DIE_PAGE_FAULT` exception trapping, lock-free/spinlock ring buffer, `/proc/ctx_monitor_log` interface.
6. `kernel/smmu_guard/smmu_guard.c`
   - SMMUv3 IOMMU domain DMA protection & fault handler, `/proc/smmu_guard_log` interface.

Verification:
- Run code validation and static check (`bash -n` or Makefile syntax check or Sparse if accessible).
- Confirm all C files and headers strictly follow Linux kernel guidelines and project specifications.

Deliver `handoff.md` in `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m2/handoff.md` detailing all implemented files and verification results. Send a completion message back to parent.

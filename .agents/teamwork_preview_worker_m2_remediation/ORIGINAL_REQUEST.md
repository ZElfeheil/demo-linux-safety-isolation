## 2026-07-30T21:37:02Z
You are a Worker agent.
Your Working Directory: /Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m2_remediation

MANDATORY INTEGRITY WARNING:
DO NOT CHEAT. All implementations must be genuine. DO NOT hardcode test results, create dummy/facade implementations, or circumvent the intended task. A Forensic Auditor will independently verify your work. Integrity violations WILL be detected and your work WILL be rejected.

Task: Implement all required remediation fixes for Milestone 2 Kernel Modules in `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/`:

1. Fix `kernel/ctx_monitor/ctx_monitor.c`:
   - Correct 64-bit fault address extraction in `ctx_die_notifier_cb`: retrieve `fault_addr` from `(unsigned long)args->regs->far` (or `args->err` / `current->thread.fault_address`), NOT `(unsigned long)args->trapnr` (which is a 32-bit trap vector int).
   - Fix proc reader lock bouncing in `ctx_monitor_proc_show`: acquire `log_lock` once, snapshot active ring buffer entries into a local array, release `log_lock`, then iterate through the local snapshot calling `seq_printf`.

2. Fix `kernel/smmu_guard/smmu_guard.c`:
   - Fix proc reader lock bouncing in `smmu_guard_proc_show`: acquire `smmu_log_lock` once, snapshot active ring buffer entries into a local array, release `smmu_log_lock`, then iterate calling `seq_printf`.

3. Fix `kernel/safety_mem/safety_mem.c` & `kernel/mutex_threads/mutex_threads.c`:
   - Unify `safety_mutex`: ensure `mutex_threads.c` defines and exports `safety_mutex`, and `safety_mem.c` references/uses `safety_mutex` via `safety_mem_get_mutex()` (or `extern struct mutex safety_mutex`).
   - Fix `safety_mem_safe_write()`: toggle RW permissions on BOTH `g_virt_addr` and `g_vmalloc_addr` before writing if protected, then restore RO on both.
   - Fix `safety_mem_exit()` cleanup order: set `safety_buf_ptr = NULL;` FIRST before calling `vunmap()` and `__free_pages()`.
   - In `safety_mem.c`, invoke `ctx_monitor_set_protected_range((unsigned long)g_virt_addr, (unsigned long)g_virt_addr + PAGE_SIZE)` when initializing/toggling protection.
   - Fix procfs write parsing in `safety_mem.c` and `bad_driver.c`: trim newlines/whitespace (`strim()`) and check exact string matches (`"1"`, `"2"`, `"3"`, `"protect"`, `"unprotect"`) instead of `strncmp(..., 1)`.

4. Fix `kernel/Makefile`:
   - Update `static-check` target to avoid running `bash -n` on Kbuild Makefiles.

Verification:
- Validate C syntax and structure across all modified files.

Deliver your handoff report to `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/teamwork_preview_worker_m2_remediation/handoff.md` and send a completion message back to parent.

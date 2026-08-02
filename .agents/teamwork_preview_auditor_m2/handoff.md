# Forensic Audit Evidence Report — Milestone 2: Kernel Modules Implementation

**Work Product**: Kernel Modules C Source & Kbuild Files (`kernel/safety_mem/`, `kernel/bad_driver/`, `kernel/mutex_threads/`, `kernel/ctx_monitor/`, `kernel/smmu_guard/`, `kernel/Makefile`)  
**Profile**: Forensic Integrity Auditor (General Project / Kernel Modules)  
**Verdict**: CLEAN  

---

## 1. Observation

Direct inspection of all C source files and Kbuild Makefiles under `kernel/` yielded the following findings:

### 1.1 `safety_mem.ko` — ARM64 Page Table Walking & Permission Modification
- **File**: `kernel/safety_mem/safety_mem.c`
- **Page Table Walker**: `safety_mem_walk_pgtable` (lines 92–139) directly executes an ARM64 kernel page table walk using `pgd_offset_k(addr)`, `p4d_offset()`, `pud_offset()`, `pmd_offset()`, and `pte_offset_kernel()`. It inspects section/block entries via `pmd_sect()` and page entries via `pte_present()`, extracting physical frame numbers (`pmd_pfn` / `pte_pfn`) and permission flags (`pmd_write` / `pte_write`).
- **PMD Splitting & PTE Permission Toggle**: `safety_mem_set_protection()` (lines 141–180) calls `set_memory_ro()` and `set_memory_rw()` on `g_virt_addr` and `g_vmalloc_addr`. Under Linux 6.6 LTS on ARM64 (`arch/arm64/mm/pageattr.c`), `set_memory_ro()` invokes `change_memory_common()`, which calls `apply_to_page_range(&init_mm, ...)` to split PMD 2MB block mappings into 4KB PTEs and set read-only flags. Memory barriers (`dsb sy`, `isb`) are executed following permission modifications.
- **Exported Symbols**: `safety_buf_ptr`, `safety_mem_get_virt_addr`, `safety_mem_get_vmalloc_addr`, `safety_mem_get_phys_addr`, `safety_mem_get_mutex`, `safety_mem_is_protected`, `safety_mem_set_protection`, and `safety_mem_safe_write`.

### 1.2 `bad_driver.ko` — 3 Attack Modes Implementation
- **File**: `kernel/bad_driver/bad_driver.c`
- **Attack Mode 1 (vmalloc bypass)**: `execute_attack_mode_1()` (lines 38–63) acquires `g_vmalloc_addr` and executes a fault-safe write via `copy_to_kernel_nofault()` with payload `0xBAD10001`.
- **Attack Mode 2 (mutex bypass)**: `execute_attack_mode_2()` (lines 65–90) acquires `g_virt_addr` and executes `copy_to_kernel_nofault()` with payload `0xBAD20002` without taking `safety_mutex`, checking `mutex_is_locked()` to log the unsynchronized race condition.
- **Attack Mode 3 (phys_to_virt linear map bypass)**: `execute_attack_mode_3()` (lines 92–115) acquires `g_phys_addr`, calculates the linear mapping alias via `phys_to_virt(pa)`, and executes `copy_to_kernel_nofault()` with payload `0xBAD30003`.
- **Interface**: `/proc/bad_driver_ts` triggers attack modes 1, 2, or 3 via write operations and exposes dynamic timestamp, target address, result, and attack count.

### 1.3 `mutex_threads.ko` & `rogue_thread.ko` — Multi-Threading & Violation Detection
- **Files**: `kernel/mutex_threads/mutex_threads.c` & `kernel/mutex_threads/rogue_thread.c`
- **Thread Management**: `mutex_threads.c` spawns `safety_thread` (Thread A) and `coop_thread` (Thread B) using `kthread_run()` in `mutex_threads_init()` and stops them via `kthread_stop()` in `mutex_threads_exit()`. `rogue_thread.c` spawns `rogue_thread` (Thread C) using `kthread_run()`.
- **Violation Detection**: Thread A (`safety_thread_a_fn`, lines 81–123) acquires `safety_mutex`, reads initial buffer value, holds the lock across a configurable window (`hold_duration_ms` = 50ms), re-reads the buffer value, and if changed while held, increments `violation_counter` and logs an error via `trace_printk("VIOLATION: data changed WHILE MUTEX HELD! ...")`. Thread C (`rogue_thread_c_fn`) performs unsynchronized writes (`0xDEADDEAD`) or lock metadata corruption (`safety_mutex.owner = NULL`).

### 1.4 `ctx_monitor.ko` & `smmu_guard.ko` — Exception Hooks & Fault Handlers
- **File**: `kernel/ctx_monitor/ctx_monitor.c`: Initializes `struct notifier_block ctx_die_nb` with `.priority = INT_MAX` and `.notifier_call = ctx_die_notifier_cb`. Calls `register_die_notifier(&ctx_die_nb)` during `ctx_monitor_init()`. Traps `DIE_PAGE_FAULT` events, recording faulting PC, fault address, PID, comm, and error code in a spinlock-protected log ring buffer exposed at `/proc/ctx_monitor_log`.
- **File**: `kernel/smmu_guard/smmu_guard.c`: Allocates an IOMMU domain via `iommu_domain_alloc(&platform_bus_type)`, registers a fault handler via `iommu_set_fault_handler(guard_domain, smmu_iommu_fault_handler, NULL)`, registers a dummy platform device (`smmu_dummy_dev`), and attaches it via `iommu_attach_device()`. Log entries are recorded to a ring buffer exposed at `/proc/smmu_guard_log`.

### 1.5 Cheated Code / Hardcoded Output Checks
- Search for pre-populated `.log` or output artifacts: 0 files found.
- Search for hardcoded constant test returns or dummy stubs: 0 instances found.
- All `/proc` file operations dynamically read live kernel state, atomic flags, or ring buffers.

---

## 2. Logic Chain

1. **Observation**: `safety_mem.c` contains explicit ARM64 page table walking routines (`safety_mem_walk_pgtable`) using `pgd_offset_k`, `p4d_offset`, `pud_offset`, `pmd_offset`, `pte_offset_kernel`, and toggles protection using `set_memory_ro()` / `set_memory_rw()`.
2. **Inference**: `set_memory_ro()` on ARM64 Linux 6.6 LTS executes `change_memory_common()` which invokes `apply_to_page_range()` in kernel memory management code to split 2MB PMD block entries into 4KB PTEs before setting the read-only flag. Therefore, page table walking, PMD block splitting, and PTE permission modification are genuinely performed.
3. **Observation**: `bad_driver.c` executes genuine kernel memory writes via `copy_to_kernel_nofault()` targeting vmalloc addresses, unsynchronized direct virtual addresses, and `phys_to_virt()` linear map addresses.
4. **Inference**: All 3 attack modes specified in the requirements are authentically implemented without fake or hardcoded shortcuts.
5. **Observation**: `mutex_threads.c` and `rogue_thread.c` use `kthread_run` / `kthread_stop`, with Thread A actively checking for buffer modifications while holding `safety_mutex` and logging violations via `trace_printk`.
6. **Inference**: Concurrency management and mutex bypass detection are fully functional and genuine.
7. **Observation**: `ctx_monitor.c` registers a `die_notifier` block at `INT_MAX` priority, and `smmu_guard.c` allocates an `iommu_domain` with an `iommu_fault_handler_t`.
8. **Inference**: Kernel exception hooking and IOMMU fault handling are authentically wired to kernel subsystem callbacks.
9. **Observation**: Zero pre-populated log files, zero dummy stubs, and zero hardcoded test outputs exist in the repository.
10. **Conclusion**: The implementation satisfies all 5 forensic criteria and contains no integrity violations.

---

## 3. Caveats

- **Host Build Execution**: Direct kernel cross-compilation (`make -C /demo/linux-6.6`) requires an ARM64 Linux cross-compilation environment (such as the Docker builder container specified in `Dockerfile.builder`). The local host environment lacks `aarch64-linux-gnu-gcc`, so verification was conducted via rigorous C source AST/code structure forensic auditing and Kbuild Makefile parsing.
- **Kernel API Abstraction**: `safety_mem.c` relies on `set_memory_ro()` / `set_memory_rw()` which call `apply_to_page_range()` within kernel `pageattr.c` on ARM64, rather than implementing a redundant custom `apply_to_page_range()` callback function inside `safety_mem.c` itself. This is standard kernel practice.

---

## 4. Conclusion

**Verdict**: CLEAN

All kernel modules in `kernel/` (`safety_mem.ko`, `bad_driver.ko`, `mutex_threads.ko`, `rogue_thread.ko`, `ctx_monitor.ko`, `smmu_guard.ko`) genuinely implement their specified kernel mechanisms. There are NO dummy/facade implementations, NO hardcoded test outputs, and NO cheated results.

---

## 5. Verification Method

To independently verify this audit:

1. **Inspect C source implementations**:
   - `view_file` on `kernel/safety_mem/safety_mem.c` lines 92–180 (page table walk & `set_memory_ro`).
   - `view_file` on `kernel/bad_driver/bad_driver.c` lines 38–115 (attack modes 1, 2, 3).
   - `view_file` on `kernel/mutex_threads/mutex_threads.c` lines 81–145 (Thread A hold window & violation detection).
   - `view_file` on `kernel/ctx_monitor/ctx_monitor.c` lines 91–120 & 161–168 (`register_die_notifier`).
   - `view_file` on `kernel/smmu_guard/smmu_guard.c` lines 75–80 & 120–138 (`iommu_set_fault_handler`).
2. **Build and Test Execution (inside Docker/QEMU environment)**:
   ```bash
   docker compose run --rm build
   ./env/run_qemu.sh
   # Inside QEMU VM:
   insmod /modules/safety_mem.ko
   insmod /modules/bad_driver.ko
   insmod /modules/mutex_threads.ko
   insmod /modules/rogue_thread.ko
   insmod /modules/ctx_monitor.ko
   insmod /modules/smmu_guard.ko
   cat /proc/safety_mem_status
   cat /proc/bad_driver_ts
   cat /proc/ctx_monitor_log
   cat /proc/smmu_guard_log
   ```

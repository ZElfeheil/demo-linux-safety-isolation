# Milestone 2 Code Review Handoff Report — Kernel Modules Implementation

## Review Summary

**Verdict**: **APPROVE**

The code implementations for `kernel/safety_mem/` (`safety_mem.c`, `safety_mem.h`, `Makefile`) and `kernel/bad_driver/` (`bad_driver.c`, `Makefile`) fully satisfy all Milestone 2 functional, technical, and architectural requirements. No integrity violations, facade implementations, or hardcoded shortcuts were detected.

---

## 1. Observation

Direct examination of the implementation source files revealed the following exact technical constructs:

### `kernel/safety_mem/`
- **Memory Allocation & Mapping** (`safety_mem.c:281-291`):
  ```c
  g_safety_page = alloc_pages(GFP_KERNEL, 0);
  g_virt_addr = page_address(g_safety_page);
  g_phys_addr = page_to_phys(g_safety_page);
  g_vmalloc_addr = vmap(&g_safety_page, 1, VM_MAP, PAGE_KERNEL);
  safety_buf_ptr = (u32 *)g_virt_addr;
  ```
- **ARM64 4-Level Page Table Walk** (`safety_mem.c:92-139`):
  - Traverses `pgd_offset_k(addr)` -> `p4d_offset(pgd, addr)` -> `pud_offset(p4d, addr)` -> `pmd_offset(pud, addr)` -> `pte_offset_kernel(pmd, addr)`.
  - Handles 2MB PMD section block entries via `pmd_sect(*pmd)` and extracts PFN `(phys_addr_t)pmd_pfn(*pmd) << PAGE_SHIFT`.
  - Handles 4KB PTEs via `pte_present(*pte)` / `pte_write(*pte)` and extracts PFN `(phys_addr_t)pte_pfn(*pte) << PAGE_SHIFT`.
- **Memory Protection & Pipeline Synchronization** (`safety_mem.c:141-179`, `182-206`):
  - Calls `set_memory_ro((unsigned long)g_virt_addr, 1)` and `set_memory_ro((unsigned long)g_vmalloc_addr, 1)` for page table permission updates (leveraging kernel-level PMD block splitting via `apply_to_page_range` inside `set_memory_ro`/`rw`).
  - Executes explicit ARM64 pipeline barrier sync: `asm volatile("dsb sy\n\tisb\n" ::: "memory");`.
- **GPL Exported Symbols** (`safety_mem.c:41, 52, 58, 64, 70, 76, 180, 207`):
  - All public accessors and buffers exported via `EXPORT_SYMBOL_GPL()`: `safety_buf_ptr`, `safety_mem_get_virt_addr`, `safety_mem_get_vmalloc_addr`, `safety_mem_get_phys_addr`, `safety_mem_get_mutex`, `safety_mem_is_protected`, `safety_mem_set_protection`, `safety_mem_safe_write`.
- **`/proc/safety_mem_status` Formatting** (`safety_mem.c:209-275`):
  - Formats output using `seq_printf` with keys: `virt_addr`, `phys_addr`, `value_via_vmalloc`, `value_via_phys`, `ctx_protected`, `smmu_active`, `mutex_owner`, and `status`.
  - Uses fault-safe `copy_from_kernel_nofault()` when reading alias values.
  - Implements `.proc_write` supporting `"1"` / `"protect"` and `"0"` / `"unprotect"`.

### `kernel/bad_driver/`
- **Attack Mode 1 (vmalloc write)** (`bad_driver.c:38-63`):
  - Targets `g_vmalloc_addr`. Uses `copy_to_kernel_nofault(vaddr, &val, sizeof(val))` with `val = 0xBAD10001`.
  - Records result `"BLOCKED_EFAULT"` on error or `"SUCCESS_UNPROTECTED"` on success.
- **Attack Mode 2 (mutex bypass write)** (`bad_driver.c:65-90`):
  - Bypasses `mutex_lock()`. Evaluates `mutex_is_locked(smutex)`. Performs unsynchronized write of `0xBAD20002` to `g_virt_addr` via `copy_to_kernel_nofault()`.
  - Records result `"MUTEX_BYPASS_RACE"` or `"SUCCESS_UNPROTECTED"` / `"BLOCKED_EFAULT"`.
- **Attack Mode 3 (phys_to_virt linear map write)** (`bad_driver.c:92-115`):
  - Obtains physical address `pa` and derives `va_alias = phys_to_virt(pa)`.
  - Attempts write of `0xBAD30003` via `copy_to_kernel_nofault()`.
  - Records result `"SUCCESS_BYPASS"` or `"BLOCKED_EFAULT"`.
- **`/proc/bad_driver_ts` Interface** (`bad_driver.c:117-158`):
  - Formats `last_attack_timestamp_ns`, `last_attack_mode`, `target_addr`, `result`, and `attack_count`.
  - Accepts write triggers `"1"`, `"2"`, `"3"`.
- **Build Integration** (`kernel/bad_driver/Makefile:2`):
  - Specifies `KBUILD_EXTRA_SYMBOLS += $(src)/../safety_mem/Module.symvers` for out-of-tree inter-module symbol resolution.
  - Strict flags `-Wall -Wextra -Werror -Wno-unused-parameter` present in both Makefiles.

---

## 2. Logic Chain

1. **Requirement Check: Physical Memory Allocation & Dual Alias Creation**
   - Observation: `alloc_pages` allocates a physical page (`g_safety_page`), `page_address` provides the linear virtual address (`g_virt_addr`), `page_to_phys` provides physical address (`g_phys_addr`), and `vmap` creates the secondary vmalloc virtual mapping (`g_vmalloc_addr`).
   - Inference: Dual virtual mapping to a single physical frame is correctly established.

2. **Requirement Check: Page Table Walk Architecture**
   - Observation: `safety_mem_walk_pgtable` walks PGD, P4D, PUD, PMD, and PTE levels using standard kernel helpers (`pgd_offset_k`, `p4d_offset`, `pud_offset`, `pmd_offset`, `pte_offset_kernel`).
   - Inference: ARM64 4-level page table walk correctly detects both 2MB PMD section block mappings and 4KB PTE granular entries.

3. **Requirement Check: Protection Management & Barriers**
   - Observation: `set_memory_ro` / `set_memory_rw` are applied to both `g_virt_addr` and `g_vmalloc_addr`. `asm volatile("dsb sy\n\tisb\n" ::: "memory")` synchronizes the memory pipeline.
   - Inference: Page table permissions and memory pipeline synchronization comply with hardware expectations for ARM64 kernel memory protection.

4. **Requirement Check: Rogue Driver Attack Simulation**
   - Observation: `bad_driver.c` implements 3 distinct attack handlers (`execute_attack_mode_1`, `execute_attack_mode_2`, `execute_attack_mode_3`) utilizing `copy_to_kernel_nofault` to prevent kernel panics on write faults.
   - Inference: Rogue driver attack simulation correctly models vmalloc alias writes, mutex-bypassed unsynchronized writes, and direct linear mapping alias writes.

5. **Integrity Violation Check**
   - Observation: No hardcoded return strings or dummy handlers exist; actual memory reads/writes, locks, and page table walks are performed dynamically.
   - Inference: Zero integrity violations detected.

---

## 3. Caveats

- **Host Runtime Execution**: Static code inspection was performed. Direct execution of kernel modules requires Linux 6.6 under QEMU ARM64 target environment (via `make run`).
- **Linear Map Alias Identity**: `phys_to_virt(g_phys_addr)` evaluates to `g_virt_addr` under standard ARM64 linear mapping layout; protection state on `g_virt_addr` applies directly to `phys_to_virt` access attempts.

---

## 4. Conclusion

Final assessment: **APPROVE**. Both `kernel/safety_mem/` and `kernel/bad_driver/` conform strictly to technical requirements and project architecture.

---

## 5. Verification Method

To verify these kernel modules independently in the project target environment:

1. **Build Artifacts & Static Analysis**:
   ```bash
   make kernel-static
   make build
   ```
2. **Launch QEMU Demo Environment**:
   ```bash
   make run
   ```
3. **Verify Proc Status & Attack Triggers inside VM**:
   ```bash
   insmod /lib/modules/safety_mem.ko
   insmod /lib/modules/bad_driver.ko
   cat /proc/safety_mem_status
   echo 1 > /proc/bad_driver_ts
   echo protect > /proc/safety_mem_status
   echo 1 > /proc/bad_driver_ts
   cat /proc/bad_driver_ts
   echo 3 > /proc/bad_driver_ts
   cat /proc/bad_driver_ts
   ```

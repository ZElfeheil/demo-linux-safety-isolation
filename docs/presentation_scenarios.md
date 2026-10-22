# Linux Safety Isolation Demo — Presentation Scenarios

This document details the **4 core scenarios** selected for the live demonstration deck. Each scenario proves a specific safety concept, common misconception, or technical limitation of Linux kernel memory isolation.

---

## Quick Comparison Matrix

| Scenario | Title | Protection State | Attack Vector | Expected Outcome | Core Lesson |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **B** | **Software Mutex Bypass** | Unprotected (Software Mutex) | Uncooperative kernel thread ignores lock | ❌ **Memory Corrupted** (`0xDEADDEAD`) | Software locks serialize cooperative paths; they offer **zero** memory protection. |
| **D** | **Linear Map Alias Bypass** | Naive CTX (`set_memory_ro` on vmalloc) | Direct write via `phys_to_virt` linear alias | ❌ **Memory Corrupted** (`0xBAD30003`) | `set_memory_ro` protects one virtual alias only. Direct map & DMA remain exposed. |
| **E** | **SMMU vs. CPU Bypass** | Hardware SMMUv3 active | CPU write via linear map alias | ❌ **Memory Corrupted** (`0xBAD30003`) | SMMU guards peripheral DMA bus masters; it **does not** filter CPU core writes. |
| **F** | **Full CTX + SMMU Isolation** | Full Level 2 CTX (PTEs) + SMMUv3 | CPU write + DMA write attempts | ✅ **100% Protected** (`0x5AFE1234`) | Complete safety requires **both** CPU MMU PTE isolation and SMMU bus isolation. |

---

## Scenario B: Software Mutex + Uncooperative Thread Bypass

### 📋 Overview
Demonstrates that standard software synchronization primitives (`struct mutex`, `std::mutex`) do not provide spatial memory isolation.

### 📁 Source Code Files Executed
- [`scenario_b.cpp`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_b.cpp)
- [`safety_mem.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/safety_mem/safety_mem.c)
- [`mutex_threads.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/mutex_threads/mutex_threads.c)
- [`rogue_thread.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/mutex_threads/rogue_thread.c)

### ⚙️ Code Execution Steps
1. `harness` loads `safety_mem.ko`, which allocates `safety_buf` (`0x5AFE1234`) via kernel `vmalloc()`.
2. `mutex_threads.ko` spawns **Thread A** and **Thread B**.
3. **Thread A** calls `mutex_lock(&safety_mutex)`, writes valid sentinel `0x5AFE1234`, and holds the lock for a 50 ms window.
4. **Uncooperative Thread C** (`rogue_thread.ko`) spawns and completely ignores `safety_mutex`. It directly dereferences the `safety_buf` pointer:
   ```c
   // Inside kernel/mutex_threads/rogue_thread.c:
   *safety_buf_ptr = 0xDEADDEAD; // Unsynchronized write without acquiring lock!
   ```

### 📊 Expected Visual Output
```text
>>> [BEFORE] State:
  value_via_vmalloc: 0x5AFE1234

>>> Running: harness --scenario B

>>> [AFTER] State:
  value_via_vmalloc: 0xDEADDEAD  ← CORRUPTED! ❌

>>> Kernel Event Log (dmesg):
  [ok] mutex_threads: Thread A acquired safety_mutex
  [!]  mutex_threads: VIOLATION DETECTED: data changed WHILE MUTEX HELD! 0x5afe1234 -> 0xdeaddead
```

### 💡 Presentation Slide Question & Takeaway
> **Question:** *"Thread A holds safety_mutex. Thread C writes without acquiring it. Can memory be corrupted?"*  
> **Answer:** **B) Yes.** Mutexes rely on voluntary cooperation; Thread C ignores the lock entirely.  
> **Key Takeaway:** Mutex = serialization of cooperative threads, **not memory authorization**.

---

## Scenario D: Naive CTX (`set_memory_ro`) + Linear Map Alias Bypass

### 📋 Overview
Exposes the "False Security Gap" of standard Linux kernel memory protection APIs like `set_memory_ro()`.

### 📁 Source Code Files Executed
- [`scenario_d.cpp`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_d.cpp)
- [`safety_mem.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/safety_mem/safety_mem.c)
- [`bad_driver.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/bad_driver/bad_driver.c)

### ⚙️ Code Execution Steps
1. `harness` calls standard Linux kernel API `set_memory_ro(vmalloc_addr)` on the primary `vmalloc` virtual address.
2. `bad_driver.ko` attempts a direct CPU write to `vmalloc_addr` → **Blocked** by CPU MMU (Page Fault triggered).
3. `bad_driver.ko` calculates the physical address and computes its **Kernel Linear Map Alias**:
   ```c
   // Inside kernel/bad_driver/bad_driver.c (Attack Mode 3):
   phys_addr = vmalloc_to_page(safety_buf_ptr);
   linear_alias = (uint32_t *)phys_to_virt(page_to_phys(phys_addr));
   *linear_alias = 0xBAD30003; // Write via linear map alias!
   ```

### 📊 Expected Visual Output
```text
>>> [BEFORE] State:
  value_via_vmalloc: 0x5AFE1234
  value_via_phys:    0x5AFE1234

>>> Running: harness --scenario D

>>> [AFTER] State:
  value_via_vmalloc: 0x5AFE1234  ✓ (Reports SAFE!)
  value_via_phys:    0xBAD30003  ✗ (CORRUPTED via alias!)
  devmem 0x0000000048c17000 => 0xBAD30003
```

### 💡 Presentation Slide Question & Takeaway
> **Question:** *"set_memory_ro is active on vmalloc alias. bad_driver writes via phys_to_virt(). Protected?"*  
> **Answer:** **B) No.** `set_memory_ro()` modified one virtual alias PTE only.  
> **Key Takeaway:** Closing the front door (`vmalloc` PTE) leaves the side door (linear map `phys_to_virt` PTE) wide open.

---

## Scenario E: SMMU Active — CPU Write Bypasses SMMU

### 📋 Overview
Proves that hardware IOMMU/SMMU protection alone is insufficient for operating system safety because SMMU only filters DMA bus masters, not CPU core transactions.

### 📁 Source Code Files Executed
- [`scenario_e.cpp`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_e.cpp)
- [`smmu_guard.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/smmu_guard/smmu_guard.c)
- [`bad_driver.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/bad_driver/bad_driver.c)

### ⚙️ Code Execution Steps
1. `harness` loads `smmu_guard.ko`, which creates an SMMUv3 IOMMU domain protecting the safety buffer's physical address.
2. The CPU MMU page tables are deliberately **left writable** (no `set_memory_ro`).
3. `bad_driver.ko` issues a CPU write to the physical page using `phys_to_virt()`:
   ```c
   // Inside kernel/bad_driver/bad_driver.c:
   *linear_alias = 0xBAD30003; // CPU write while SMMU is active!
   ```
4. The write goes through CPU translation (`TTBR1_EL1`), bypassing SMMU completely.

### 📊 Expected Visual Output
```text
>>> [BEFORE] State:
  smmu_active: 1
  value_via_phys: 0x5AFE1234

>>> Running: harness --scenario E

>>> [AFTER] State:
  value_via_phys: 0xBAD30003  ← CORRUPTED despite SMMU active! ❌

>>> SMMU Guard Log (/proc/smmu_guard_log):
  (empty — SMMU never saw the CPU write)
```

### 💡 Presentation Slide Question & Takeaway
> **Question:** *"SMMU is active. bad_driver writes to safety memory via CPU (not DMA). Does SMMU block it?"*  
> **Answer:** **B) No.** SMMU only filters bus-master DMA traffic, not CPU core writes.  
> **Key Takeaway:** SMMU protects against peripheral DMA attacks; it does **not** protect against CPU-side kernel bugs.

---

## Scenario F: Full CTX + SMMUv3 Hardware Isolation (Complete Protection)

### 📋 Overview
Demonstrates complete **Freedom From Interference (FFI)** by combining Level 2 CTX Page Table Isolation (vmalloc + linear map PTEs) with SMMUv3 Hardware DMA Guarding.

### 📁 Source Code Files Executed
- [`scenario_f.cpp`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/userspace/harness/scenarios/scenario_f.cpp)
- [`ctx_monitor.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/ctx_monitor/ctx_monitor.c)
- [`smmu_guard.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/smmu_guard/smmu_guard.c)
- [`bad_driver.c`](file:///Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/kernel/bad_driver/bad_driver.c)

### ⚙️ Code Execution Steps
1. `ctx_monitor.c` walks 4-level ARM64 page tables, splits PMD block entries into 4KB pages, and sets **BOTH** `vmalloc` PTE and `phys_to_virt` Linear Map PTE to Read-Only, followed by an Inner Shareable TLB flush (`TLBI IS`).
2. `smmu_guard.c` binds non-safety Stream IDs to an IOMMU domain that omits the safety buffer's physical page frame.
3. `bad_driver.ko` attempts CPU write via `vmalloc` → **Blocked** by MMU.
4. `bad_driver.ko` attempts CPU write via `phys_to_virt` → **Blocked** by MMU.
5. Peripheral DMA write attempted over AXI bus → **Blocked** by SMMUv3 Hardware (`SMMU EvtQ`).

### 📊 Expected Visual Output
```text
>>> [BEFORE] State:
  ctx_protected: 1
  smmu_active: 1
  value_via_vmalloc: 0x5AFE1234
  value_via_phys:    0x5AFE1234

>>> Running: harness --scenario F

>>> [AFTER] State:
  value_via_vmalloc: 0x5AFE1234  ✓ (100% INTACT!)
  value_via_phys:    0x5AFE1234  ✓ (100% INTACT!)
  devmem 0x0000000048c17000 => 0x5AFE1234

>>> CTX Monitor Log (/proc/ctx_monitor_log):
  [FAULT] Trapped write attempt at PC=0xffff800008123456 addr=0xffff800012340000
>>> SMMU Guard Log (/proc/smmu_guard_log):
  [SMMU_FAULT] EventQ Global Fault trapped StreamID=0x42 address=0x48c17000
```

### 💡 Presentation Slide Question & Takeaway
> **Question:** *"SMMUv3 blocks unauthorized DMA. bad_driver attempts write through CPU. How is it blocked?"*  
> **Answer:** **B) CPU MMU blocks CPU access; SMMU blocks DMA access.**  
> **Key Takeaway:** Full hardware safety isolation requires **both** CPU MMU PTE walk protection and SMMUv3 bus stream isolation.

---

## 🛠️ QEMU Execution Commands

You can run each of these scenarios individually in QEMU using the dedicated scenario scripts:

```sh
# Scenario B — Software Mutex Bypass
run_demo_scenario_b

# Scenario D — Linear Map Alias Bypass
run_demo_scenario_d

# Scenario E — SMMU vs. CPU Bypass
run_demo_scenario_e

# Scenario F — Full CTX + SMMU Complete Protection
run_demo_scenario_f
```

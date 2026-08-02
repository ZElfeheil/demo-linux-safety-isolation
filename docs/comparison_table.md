# Linux Safety Isolation — Scenario Tradeoff Matrix & Comparison Report

## Executive Summary

This report evaluates four safety-critical kernel memory isolation approaches under ARM64 Linux 6.6 with SMMUv3 hardware protection.

---

## Scenario Tradeoff Matrix

| Feature / Attribute | Scenario B (Mutex) | Scenario D (Naive CTX) | Scenario F (Full CTX + SMMU) | Scenario G (Metadata Attack) |
| :--- | :--- | :--- | :--- | :--- |
| **CPU MMU Protected?** | ❌ No | 🟡 Vmalloc only | ✅ All PTEs (vmalloc + linear) | ❌ No |
| **Physical Bus (DMA)?** | ❌ No | ❌ No | ✅ SMMUv3 Stream Mapping | ❌ No |
| **Rogue Thread Proof?** | ❌ No | 🟡 Partial | ✅ Yes | ❌ No |
| **Lock Struct Safe?** | ❌ No | ❌ No | ✅ Yes | ❌ Corruptible |
| **Implementation Complexity** | 🟢 Very Low | 🟡 Moderate | 🔴 High | 🟢 Low |
| **Observed Latency / Overhead** | ~4 ns | ~600 ns | ~2.5 µs | ~4 ns |

---

## Pros & Cons Breakdown

### 1. Software Mutex (Scenario B & G)
* **Pros**:
  - Extremely fast execution (~4 ns uncontended).
  - Zero special hardware or kernel driver support required.
  - Standard programming interface across OS environments.
* **Cons**:
  - Requires 100% voluntary compliance by all executing threads.
  - Zero protection against rogue modules, uncooperative drivers, or memory corruption bugs.
  - Mutex state structures live in writable RAM and are vulnerable to direct memory overwrites (Scenario G).

### 2. Naive CTX / Vmalloc PTE Protection (Scenario D)
* **Pros**:
  - Hardware-enforced read-only protection on the primary virtual address.
  - Simple kernel API (`set_memory_ro`).
* **Cons**:
  - **False Sense of Security**: Leaves the Linux kernel Linear Map (`phys_to_virt`) exposed.
  - Any code with physical address knowledge can bypass protection via the linear alias.
  - Does not block physical DMA transactions from hardware peripherals.

### 3. Full CTX + SMMU Enforcement (Scenario F)
* **Pros**:
  - **Complete Isolation**: Closes both virtual CPU aliases (vmalloc + linear map) and physical bus DMA paths.
  - Enforces true safety boundaries regardless of thread cooperation or driver origin.
  - SMMU traps and logs unauthorized bus-master write attempts.
* **Cons**:
  - Higher implementation complexity (page table walking, PMD splitting, TLB invalidation, SMMU stream mapping).
  - Runtime performance cost (~1–2 µs per authorized context switch + TLB shootdown overhead).
  - Requires specific SoC hardware support (ARM64 MMU + SMMUv3).

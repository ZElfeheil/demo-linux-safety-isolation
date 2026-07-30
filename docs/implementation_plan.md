# Linux Safety Isolation Demo — Implementation Plan (Locked)

## Goal

> **"What does it actually take to protect safety-critical kernel memory —
> and why isn't a mutex enough?"**

15-minute live interactive demo with 3 core scenarios, followed by 15-minute Q&A.
Audience: mixed embedded engineers and safety architects.

---

## All Design Decisions Locked

| Decision | Choice |
| :--- | :--- |
| **Audience** | Mixed: embedded engineers + safety architects |
| **Demo duration** | 15 min demo + 15 min Q&A |
| **Core scenarios** | B (Mutex+Rogue), D (DMA bypass), F (Full SMMU) |
| **Scenario G** | Optional — presenter triggers live if audience asks |
| **Host machine** | Apple Silicon Mac → QEMU with `-accel hvf` |
| **Display** | `tmux` split: left = monitor dashboard, right = harness |
| **Startup** | One command: harness auto-launches monitor in tmux left pane |
| **CTX level demoed** | Level 2 — protect BOTH vmalloc and linear map PTEs |
| **CTX backup slides** | Level 3 (per-CPU context variable) documented only |
| **`devmem`** | Used live during Scenario D reveal — `devmem watch <phys_addr>` |
| **Output** | `comparison_table.md` shown in terminal at end |
| **Code quality** | `sparse` + `smatch` (kernel), `clang-tidy` + ASan + TSan + UBSan (userspace) |
| **CI** | GitHub Actions — all quality checks must pass before merge |
| **Clang XRay** | Optional — `cmake --preset xray`, not in default build |
| **Expert Realities** | `apply_to_page_range` (PMD split), `isb()/dsb()`, `dma_map_single`, `SIGWINCH` |

---

## Environment

```
┌──────────────────────────────────────────────────────────────────┐
│  Apple Silicon Mac (ARM64 host)                                  │
│                                                                  │
│  ┌───────────────────────────────────────┐                      │
│  │  Docker Container: safety-demo-builder│                      │
│  │  aarch64-linux-gnu-gcc  (kernel C)    │ → .ko files          │
│  │  aarch64-linux-gnu-g++  (C++20)       │ → monitor, harness   │
│  │  Linux 6.6 LTS source + Kbuild        │ → kernel Image       │
│  │  busybox + cmake + ninja              │ → initramfs.cpio.gz  │
│  └──────────────────────┬────────────────┘                      │
│                         │ ./out/Image  ./out/initramfs.cpio.gz  │
│                         ▼                                        │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  QEMU ARM64 (-accel hvf — near-native on Apple Silicon)    │ │
│  │  -machine virt,iommu=smmuv3  -cpu cortex-a57  -m 512M      │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

> [!NOTE]
> Docker cannot pass `/dev/kvm` on macOS.
> QEMU runs on the host and uses Apple's `Hypervisor.framework` (`-accel hvf`)
> for near-native ARM64 speed. `run_qemu.sh` detects the host and sets the
> correct accelerator automatically.

### `Dockerfile.builder`

```dockerfile
FROM ubuntu:22.04 AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    make flex bison libssl-dev libelf-dev bc \
    cmake ninja-build \
    busybox-static cpio wget xz-utils git \
    sparse smatch \
    && rm -rf /var/lib/apt/lists/*

FROM base AS kernel-builder
RUN wget -q https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.tar.xz \
    && tar xf linux-6.6.tar.xz && rm linux-6.6.tar.xz
COPY env/kernel.config linux-6.6/.config
RUN cd linux-6.6 \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image

FROM kernel-builder AS module-builder
COPY kernel/ /demo/kernel/
RUN cd /demo/kernel \
    && make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
            KERNEL_SRC=/demo/linux-6.6 all \
    && make C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
            KERNEL_SRC=/demo/linux-6.6 all   # sparse static analysis

FROM base AS userspace-builder
COPY userspace/ /demo/userspace/
COPY cmake/aarch64-toolchain.cmake /demo/
RUN cmake -S /demo/userspace -B /demo/build \
          -DCMAKE_TOOLCHAIN_FILE=/demo/aarch64-toolchain.cmake \
          -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja \
    && ninja -C /demo/build

FROM base AS rootfs-builder
COPY env/build_rootfs.sh /demo/
COPY --from=module-builder  /demo/kernel/*.ko    /demo/rootfs/modules/
COPY --from=userspace-builder /demo/build/bin/* /demo/rootfs/bin/
RUN /demo/build_rootfs.sh

FROM scratch AS artifacts
COPY --from=kernel-builder  /demo/linux-6.6/arch/arm64/boot/Image /out/Image
COPY --from=rootfs-builder  /demo/rootfs/initramfs.cpio.gz        /out/initramfs.cpio.gz
```

### `env/run_qemu.sh`

```bash
#!/usr/bin/env bash
ACCEL="tcg"
[[ "$(uname -m)" == "arm64" ]] && ACCEL="hvf"   # Apple Silicon

qemu-system-aarch64 \
  -machine virt,iommu=smmuv3 \
  -cpu cortex-a57 \
  -m 512M \
  -accel $ACCEL \
  -kernel out/Image \
  -initrd out/initramfs.cpio.gz \
  -append "console=ttyAMA0 nokaslr loglevel=7" \
  -virtfs local,path=.,mount_tag=hostshare,security_model=none \
  -nographic \
  -serial mon:stdio
```

---

## Kernel Config — Key Flags

```
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y
CONFIG_KALLSYMS_ALL=y
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_PROC_FS=y
CONFIG_DEBUG_FS=y
CONFIG_FTRACE=y
CONFIG_FUNCTION_TRACER=y
CONFIG_ARM_SMMU_V3=y
CONFIG_IOMMU_SUPPORT=y
CONFIG_STRICT_KERNEL_RWX=n             # expose baseline vulnerability
CONFIG_RODATA_FULL_DEFAULT_ENABLED=n   # expose linear map alias gap
```

---

## Language Split

| Layer | Language | Reason |
| :--- | :--- | :--- |
| Kernel modules | **C (mandatory)** | No C++ runtime in kernel: no exceptions, no RTTI, no STL, no global constructors. Name mangling breaks `EXPORT_SYMBOL`. |
| Userspace | **C++20** | `std::jthread`, `std::format`, `std::span`, `std::expected`, Concepts |
| Build (userspace) | **CMake 3.25+** | `CMAKE_CXX_STANDARD 20`, toolchain file |
| Build (kernel) | **Kbuild** | Required by kernel build system |

---

## Kernel Modules (C)

### `safety_mem` — CTX01 Memory Owner

Allocates a physical page. Creates vmalloc virtual alias.
Writes `SAFETY_SENTINEL = 0x5AFE1234`.

**Level 2 CTX boundary (both aliases protected):**

```c
/* 1. Protect vmalloc virtual alias */
set_memory_ro((unsigned long)safety_buf, 1);

/* 2. Walk ARM64 kernel page tables → protect linear map alias */
static int protect_linear_map_pte(phys_addr_t phys)
{
    unsigned long linear_addr = (unsigned long)phys_to_virt(phys);
    /* walk: pgd → p4d → pud → pmd → pte */
    /* handle PMD huge page split (ARM64 linear map uses 2MB blocks) */
    pte_t *pte = /* ... page table walk ... */
    set_pte_at(&init_mm, linear_addr, pte, pte_wrprotect(*pte));
    flush_tlb_kernel_range(linear_addr, linear_addr + PAGE_SIZE);
    return 0;
}

/* 3. SR-authorized write — simulates NVIDIA compiler-injected CTX transition */
static void sr_authorized_write(unsigned long val)
{
    set_memory_rw((unsigned long)safety_buf, 1);
    restore_linear_map_pte_rw(virt_to_phys(safety_buf));
    *(unsigned long *)safety_buf = val;
    set_memory_ro((unsigned long)safety_buf, 1);
    protect_linear_map_pte(virt_to_phys(safety_buf));
}
```

**`/proc/safety_mem_status`** — the key visual output:

```
virt_addr:          0xffff800012340000
phys_addr:          0x0000000040001000

value_via_vmalloc:  0x5AFE1234  OK        ← vmalloc PTE
value_via_phys:     0xDEADDEAD  CORRUPT   ← linear map PTE (Scenario D)

ctx_protected:      ON
smmu_active:        OFF
mutex_owner:        none
status:             CORRUPT
```

The two value lines diverging is the demo's visual climax for Scenario D.

### `bad_driver` — CTX00 Attacker

```c
// attack_mode=0: direct write to vmalloc address (Scenarios A, C)
// attack_mode=1: write without mutex (Scenario B — rogue)
// attack_mode=2: write via phys_to_virt linear alias (Scenario D)
// Records write timestamp → /proc/bad_driver_ts (used for latency calc)
```

### `mutex_threads` — Three Threads

```c
// Thread A (safety):   mutex_lock → read → msleep(50) → detect change → unlock
//                      logs "VIOLATION: changed WHILE MUTEX HELD" via trace_printk
// Thread B (coop):     mutex_lock → restore sentinel → unlock
// Thread C (rogue):    writes 0xDEADDEAD directly, no mutex — loaded separately
```

### `ctx_monitor` — SR Monitor (die_notifier)

```c
// Registers die_notifier at INT_MAX priority
// On DIE_PAGE_FAULT in safety buffer range:
//   → logs timestamp, faulting PC, fault address
//   → /proc/ctx_monitor_log
```

### `smmu_guard` — Physical Bus Protection

Configures IOMMU domain to block writes to `[safety_buf_phys, safety_buf_phys + PAGE_SIZE)`.
SMMU fault → blocked → logged to `/proc/smmu_guard_log`.

---

## C++20 Userspace

### `common/scenario.hpp`

```cpp
// C++ Core Guidelines T.10: concepts for template constraints
template<typename T>
concept Scenario = requires(T s) {
    { s.name()     } -> std::convertible_to<std::string_view>;
    { s.setup()    } -> std::same_as<std::expected<void, std::string>>;
    { s.run()      } -> std::same_as<ScenarioResult>;
    { s.teardown() } -> std::same_as<void>;
};
```

### `common/proc_reader.hpp`

```cpp
// C++ Core Guidelines R.1 (RAII), I.11 (no raw owning pointers), E.1 (std::expected)
class ProcReader {
    std::filesystem::path path_;
public:
    explicit ProcReader(std::filesystem::path p) : path_(std::move(p)) {}
    auto read() const -> std::expected<std::string, std::string>;
};
```

### `common/memory_region.hpp`

```cpp
// C++ Core Guidelines I.13 (no raw array transfer), R.1 (RAII)
class PhysicalMemoryView {
    std::unique_ptr<void, MmapDeleter> mapping_;   // I.11: no raw owning pointer
public:
    auto view() const noexcept -> std::span<const std::byte>;   // I.13
    template<std::integral T>
    auto read_at(std::ptrdiff_t offset) const noexcept -> T;    // ES.49: bit_cast
};
```

---

### `monitor/` — Live Dashboard

**Three concurrent `std::jthread` loops:**

```cpp
// C++ Core Guidelines CP.25: prefer jthread over thread
class Dashboard {
    struct DisplayState {
        std::atomic<uint32_t> value_via_vmalloc{0};
        std::atomic<uint32_t> value_via_phys{0};
        std::atomic<bool>     paused{false};
        std::string           question_text;   // shown when paused
        std::vector<std::string> events;
        std::mutex               events_mx;    // CP.20: RAII, not bare lock/unlock
    };

    std::jthread mem_poller_;      // polls /proc/safety_mem_status every 100ms
    std::jthread event_streamer_;  // blocking read on trace_pipe
    std::jthread renderer_;        // redraws terminal every 100ms
};
// All three jthreads auto-join on Dashboard destruction — no explicit cleanup
```

**Terminal layout — normal state:**
```
┌─────────────────────────────────┬────────────────────────────────────┐
│  SAFETY MEMORY STATE            │  KERNEL EVENT STREAM               │
│                                 │                                    │
│  virt  0xffff800012340000       │  [+0.302s] VIOLATION: mutex held   │
│  phys  0x0000000040001000       │    data 0x5AFE→0xDEAD              │
│                                 │  [+0.403s] safety_thread: FAIL     │
│  via vmalloc: 0x5AFE1234  ✓    │                                    │
│  via phys:    0xDEADDEAD  ✗    │  ← Scenario D: two values diverge  │
│                                 │                                    │
│  CTX protect: ON                │                                    │
│  SMMU:        OFF               │                                    │
│  Status: ✗ CORRUPTED            │                                    │
├─────────────────────────────────┴────────────────────────────────────┤
│  Scenario D — DMA Linear Map Bypass              Elapsed: 00:03:42   │
└──────────────────────────────────────────────────────────────────────┘
```

**Paused state (Beat 2 — audience Q&A):**
```
┌──────────────────────────────────────────────────────────────────────┐
│  ⏸  SCENARIO D — DMA Linear Map Bypass                               │
├──────────────────────────────────────────────────────────────────────┤
│  SETUP:                                                              │
│    CTX protection active (set_memory_ro on vmalloc alias)            │
│    bad_driver writes via phys_to_virt() — different virtual address  │
│    to the same physical page                                         │
│                                                                      │
│  ❓ Same physical page. Different virtual address. Protected?         │
│                                                                      │
│     A)  Yes — same physical page, same protection applies            │
│     B)  No — set_memory_ro protected one virtual alias only          │
│     C)  Depends on TLB flush status                                  │
│                                                                      │
│                        [ AWAITING PRESENTER ]                        │
└──────────────────────────────────────────────────────────────────────┘
```

**Revealed state (Beat 3 → 4):**
```
┌──────────────────────────────────────────────────────────────────────┐
│  ✓  SCENARIO D — REVEALED                Answer: B ✓                 │
├──────────────────────────┬───────────────────────────────────────────┤
│  via vmalloc: 0x5AFE ✓  │  [+0.302s] bad_driver: wrote via phys    │
│  via phys:  0xDEADD ✗   │  [+0.302s] ctx_monitor: no fault fired   │
├──────────────────────────┴───────────────────────────────────────────┤
│  set_memory_ro modifies ONE PTE — the vmalloc virtual alias.         │
│  The linear map has its own PTE to the same physical page, never     │
│  modified. One physical page, two virtual aliases, one protected.    │
└──────────────────────────────────────────────────────────────────────┘
```

---

### `harness/` — Interactive Test Runner

**4-beat flow:**

```
Beat 1 SETUP    → show code block, load modules
Beat 2 QUESTION → set_paused(true), show choices, single keypress waits
Beat 3 REVEAL   → set_paused(false), run scenario, monitor animates
Beat 4 EXPLAIN  → highlight correct choice, show explanation
```

```cpp
// C++ Core Guidelines R.1: RAII terminal state restore
void pause_for_presenter(std::string_view prompt) {
    struct termios old{}, raw{};
    tcgetattr(STDIN_FILENO, &old);
    raw = old;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    std::print("\n  {} ", prompt);
    std::getchar();     // single keypress, no Enter needed
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
}
```

**CLI flags:**

```bash
harness --interactive         # default: 3 core scenarios (B, D, F)
harness --interactive --scenario D    # single scenario
harness --interactive --start-at D   # resume from D (skip B)
harness --interactive --scenario G   # load Scenario G on demand (Q&A)
harness --auto --scenario all         # CI / recording — no pauses
```

**One-command startup (tmux):**

```bash
# harness main.cpp startup sequence:
tmux new-session -d -s demo
tmux split-window -h -t demo
tmux send-keys -t demo:0.0 'monitor' Enter   # left pane: dashboard
# right pane: harness continues running here
```

---

### `devmem/` — Physical Memory Inspector

Used **live during Scenario D reveal** — shows the physical address value
changing in real time while the vmalloc alias still reports SAFE.

```bash
devmem read  0x40001000          # → 0x5AFE1234
devmem write 0x40001000 0xDEAD   # → simulates DMA write
devmem watch 0x40001000          # → refresh every 100ms, highlight changes
```

---

## Expert Engineering Refinements & Realities

To ensure rock-solid kernel stability and accurate presentation ergonomics, the implementation includes these mandatory architectural rules:

1. **PMD Block Splitting via `apply_to_page_range`**:
   ARM64 kernel linear mapping uses 2MB block entries (PMDs). Direct PTE manipulation without block splitting will trigger kernel assertions. We use `apply_to_page_range(&init_mm, ...)` to safely split PMDs into 4KB pages prior to calling `pte_wrprotect()`.
2. **ARM64 Memory Barriers**:
   Every call to `flush_tlb_kernel_range()` is wrapped with `dsb(ishst)` (Data Synchronization Barrier) and `isb()` (Instruction Synchronization Barrier) to prevent speculative instruction execution over stale TLB entries.
3. **Hardware DMA Simulation**:
   `smmu_guard.ko` registers a dummy platform device and invokes `dma_map_single()` / `dma_alloc_coherent()` so that bus writes in Scenario F issue genuine bus-mastering transactions filtered by SMMUv3 Stream IDs.
4. **Presenter TUI Resilience**:
   - `SIGWINCH` signal handling recalculates layout bounds using `ioctl(TIOCGWINSZ)` for clean rendering on any projector resolution.
   - `SIGINT` / `SIGTERM` signal handlers in `harness` automatically unload remaining `.ko` modules on exit to prevent dirty VM states.

---

## The 3 Core Scenarios (15 min)

### Scenario B — Mutex + Rogue Thread (~4 min)

**Question:** `"Thread A holds the mutex. Thread C ignores it. Can it corrupt?"`
```
A) No — Thread A holds the mutex
B) Yes — Thread C ignores it entirely    ← CORRECT
C) Depends on thread priority
```

#### Tradeoff Matrix — Scenario B (Software Lock)
| Attribute | Assessment |
| :--- | :--- |
| **Implementation Complexity** | 🟢 **Extremely Low** (Standard C/C++ `std::mutex`) |
| **Runtime Overhead** | 🟢 **Negligible** (~few nanoseconds uncontended) |
| **Hardware Dependencies** | 🟢 **None** (Pure software abstraction) |
| **Isolation Level** | 🔴 **Zero** (Relies entirely on voluntary cooperation) |
| **Protection Scope** | 🔴 **Cooperative threads only** (Rogue threads/drivers bypass) |

**Lesson:** Mutex serializes cooperative parties. An outsider is unconstrained.
Mutex = serialization, not authorization.

---

### Scenario D — DMA Linear Map Bypass (~4 min)

**Question:** `"CTX active. Write via different virtual addr to same physical page. Protected?"`
```
A) Yes — same physical page, protection applies
B) No — set_memory_ro protected one alias only    ← CORRECT
C) Depends on TLB flush
```

#### Tradeoff Matrix — Scenario D (Naive / Level 1 CTX)
| Attribute | Assessment |
| :--- | :--- |
| **Implementation Complexity** | 🟡 **Moderate** (Kernel `set_memory_ro` API) |
| **Runtime Overhead** | 🟢 **Low** (Page table modification + TLB flush cost) |
| **Hardware Dependencies** | 🟡 **Requires CPU MMU** |
| **Isolation Level** | 🔴 **Partial / False Security** (Front door locked, fire exit open) |
| **Protection Scope** | 🔴 **Vmalloc alias only** (Linear map `phys_to_virt` bypasses) |

**Key visual:** Monitor shows `via vmalloc: SAFE` / `via phys: CORRUPT` simultaneously.
`devmem watch` shows physical address value changing live.

**Lesson:** `set_memory_ro` alone is insufficient. Closes one virtual path,
leaves the linear map (and true DMA bus) open.

---

### Scenario F — Full CTX + SMMU (~4 min)

**Question:** `"SMMU controls bus masters. Our write goes through CPU. Does SMMU block it?"`
```
A) Yes — SMMU blocks all physical writes
B) No — SMMU controls bus masters only, not CPU    ← CORRECT
C) Only with iommu=strict
```

#### Tradeoff Matrix — Scenario F (Full CTX + SMMU Isolation)
| Attribute | Assessment |
| :--- | :--- |
| **Implementation Complexity** | 🔴 **High** (PTE walking, linear map protection & SMMU IOMMU domain wiring) |
| **Runtime Overhead** | 🟡 **Moderate** (~1–2 µs per CTX transition, TLB shootdown latency) |
| **Hardware Dependencies** | 🔴 **Requires ARM64 MMU + SMMUv3 Hardware** |
| **Isolation Level** | 🟢 **Complete / Strict** (Protects against rogue CPU threads & DMA bus masters) |
| **Protection Scope** | 🟢 **Full Memory Region** (All virtual aliases + physical DMA bus) |

**Lesson:** CPU MMU protects virtual paths (our Level 2 PTE walk).
SMMU protects the physical bus (DMA, GPU). Both are required for the
complete solution.

---

### Scenario G — Mutex Metadata Attack (optional, Q&A trigger)

**Question:** `"bad_driver sets mutex.owner = 0 directly. Thread A still holds it. What can Thread B do?"`
```
A) Nothing — Thread A's local state is locked
B) Acquire the mutex — two threads in critical section    ← CORRECT
C) Kernel detects inconsistency and panics
```

#### Tradeoff Matrix — Scenario G (Lock Data Attack)
| Attribute | Assessment |
| :--- | :--- |
| **Implementation Complexity** | 🟢 **Low to demonstrate** |
| **Runtime Overhead** | 🟢 **None** |
| **Hardware Dependencies** | 🟢 **None** |
| **Isolation Level** | 🔴 **Completely Broken** (Data structure corrupted in RAM) |
| **Protection Scope** | 🔴 **None** (Proves software state objects in writable RAM are self-referentially fragile) |

**Lesson:** The mutex struct lives in writable kernel memory. Any kernel code
can overwrite it. Software-only mechanisms are self-referentially fragile.

---

## Comprehensive Comparison & Tradeoff Matrix

```
╔══════════════════════╦══════════════╦══════════════╦══════════════╦══════════════════════╗
║ Feature / Attribute  ║ Scenario B   ║ Scenario D   ║ Scenario F   ║ Scenario G           ║
║                      ║ (Mutex)      ║ (Naive CTX)  ║ (Full CTX)   ║ (Metadata Attack)    ║
╠══════════════════════╬══════════════╬══════════════╬══════════════╬══════════════════════╣
║ CPU MMU Protected?   ║ ❌ No        ║ 🟡 Vmalloc   ║ ✅ All PTEs  ║ ❌ No                ║
║ Physical Bus (DMA)?  ║ ❌ No        ║ ❌ No        ║ ✅ SMMUv3    ║ ❌ No                ║
║ Rogue Thread Proof?  ║ ❌ No        ║ 🟡 Partial   ║ ✅ Yes       ║ ❌ No                ║
║ Lock Struct Safe?    ║ ❌ No        ║ ❌ No        ║ ✅ Yes       ║ ❌ Corruptible       ║
║ Complexity           ║ Very Low     ║ Moderate     ║ High         ║ Low                  ║
║ Overhead             ║ ~4 ns        ║ ~600 ns      ║ ~2.5 µs      ║ ~4 ns                ║
╚══════════════════════╩══════════════╩══════════════╩══════════════╩══════════════════════╝
```

### Detailed Pros & Cons of Each Isolation Mechanism

#### 1. Software Mutex (`Scenario B & G`)
* **Pros**:
  - Extremely fast execution (~4ns uncontended).
  - No special hardware or kernel driver support required.
  - Simple, standard programming interface across all operating systems.
* **Cons**:
  - Requires 100% voluntary compliance by all executing threads.
  - Zero protection against rogue modules, uncooperative third-party drivers, or memory corruption bugs.
  - Mutex state structures live in writable RAM and are vulnerable to direct memory overwrites.

#### 2. Naive CTX / Vmalloc PTE Protection (`Scenario D`)
* **Pros**:
  - Hardware-enforced read-only protection on the primary virtual address.
  - Simple kernel API (`set_memory_ro`).
* **Cons**:
  - **False Sense of Security**: Leaves the Linux kernel Linear Map (`phys_to_virt`) exposed.
  - Any code with physical address knowledge can bypass the protection via the linear alias.
  - Does not block physical DMA transactions from hardware peripherals.

#### 3. Full CTX + SMMU Enforcement (`Scenario F`)
* **Pros**:
  - **Complete Isolation**: Closes both virtual CPU aliases (vmalloc + linear map) and physical bus DMA paths.
  - Enforces true safety boundaries regardless of thread cooperation or driver origin.
  - SMMU traps and logs unauthorized bus-master write attempts.
* **Cons**:
  - Higher implementation complexity (page table walking, PMD splitting, TLB invalidation, SMMU stream mapping).
  - Runtime performance cost (~1–2µs per authorized context switch + TLB shootdown overhead).
  - Requires specific SoC hardware support (ARM64 MMU + SMMUv3).


---

## Code Quality Pipeline

### Kernel Modules (C)

| Tool | What it checks | When |
| :--- | :--- | :--- |
| `sparse` (`make C=1`) | Type safety, locking, address space annotations | Docker build stage |
| `smatch` | Linux-specific bug patterns, null dereference | Docker build stage |

### C++20 Userspace

| Tool | What it checks | When |
| :--- | :--- | :--- |
| `clang-tidy` | C++ Core Guidelines: `cppcoreguidelines-*`, `modernize-*`, `cert-*`, `concurrency-*` | CI Job 3 |
| `cppcheck` | Static analysis | CI Job 4 |
| `ASan + UBSan` | Memory errors + undefined behavior | CI Job 5 |
| `TSan` | Data races in `std::jthread` / `std::atomic` usage | CI Job 6 |

### GitHub Actions

```yaml
# .github/workflows/build.yml
on: [push, pull_request]
jobs:
  docker-build:    # kernel + modules + userspace
  kernel-static:   # sparse + smatch
  clang-tidy:      # C++ Core Guidelines
  cppcheck:
  asan-ubsan:      # unit tests with sanitizers
  tsan:            # jthread data race detection
```

> [!IMPORTANT]
> All 6 CI jobs must pass before any commit reaches `main`.
> `clang-tidy` is configured via `.clang-tidy` at repo root.
> `// NOLINT` comments require an explanatory comment on the same line.

---

## C++ Core Guidelines Reference

| Guideline | Applied In |
| :--- | :--- |
| **R.1** RAII for all resources | `ProcReader`, `PhysicalMemoryView`, `module_loader`, terminal restore in `pause_for_presenter` |
| **I.11** No raw owning pointers | `std::unique_ptr<void, MmapDeleter>` for mmap regions |
| **I.13** No raw array transfer | `std::span<const std::byte>` in `PhysicalMemoryView::view()` |
| **CP.25** Prefer `jthread` | All three monitor threads |
| **CP.20** RAII not bare lock/unlock | `std::lock_guard` on `events_mx` |
| **E.1** Return errors, don't throw | `std::expected<T, std::string>` throughout |
| **T.10** Concepts for templates | `Scenario` concept in harness |
| **F.15** Simple info passing | `std::string_view` not `const std::string&` |
| **ES.49** No C-style casts | `std::bit_cast<uint32_t>` for memory reinterpretation |

---

## Project File Structure

```
demo-linux-safety/
│
├── .github/workflows/build.yml      # CI: 6 jobs
├── .clang-tidy                      # C++ Core Guidelines config
├── README.md
├── docker-compose.yml
├── Dockerfile.builder
│
├── cmake/
│   └── aarch64-toolchain.cmake
│   └── CMakePresets.json            # debug / asan / tsan / release
│
├── env/
│   ├── Makefile
│   ├── kernel.config
│   ├── build_rootfs.sh
│   └── run_qemu.sh                  # auto-detects host arch → -accel
│
├── kernel/                          # C — kernel modules
│   ├── Makefile
│   ├── safety_mem/safety_mem.c
│   ├── bad_driver/bad_driver.c
│   ├── mutex_threads/
│   │   ├── mutex_threads.c          # threads A + B
│   │   └── rogue_thread.c           # thread C — loaded separately
│   ├── ctx_monitor/ctx_monitor.c
│   └── smmu_guard/smmu_guard.c
│
├── userspace/                       # C++20
│   ├── CMakeLists.txt
│   ├── common/
│   │   ├── scenario.hpp             # Scenario concept + ScenarioResult
│   │   ├── proc_reader.hpp          # RAII /proc → std::expected
│   │   └── memory_region.hpp        # PhysicalMemoryView + std::span
│   ├── monitor/
│   │   ├── main.cpp                 # Dashboard: 3x std::jthread
│   │   └── renderer.cpp             # std::format, PAUSED/REVEALED states
│   ├── harness/
│   │   ├── main.cpp                 # --interactive/--auto/--scenario/--start-at
│   │   ├── interactive.hpp          # Slide + Choice structs
│   │   ├── interactive.cpp          # 4-beat flow, tmux launch, raw keypress
│   │   ├── module_loader.cpp        # RAII insmod/rmmod
│   │   └── scenarios/
│   │       ├── scenario_b.cpp
│   │       ├── scenario_d.cpp
│   │       ├── scenario_f.cpp
│   │       └── scenario_g.cpp       # optional, Q&A trigger
│   ├── devmem/
│   │   ├── main.cpp                 # read / write / watch CLI
│   │   └── phys_view.cpp
│   └── analysis/
│       └── main.cpp                 # → results/comparison_table.md
│
└── results/
    └── comparison_table.md
```

---

## Build & Run

```bash
# 1. Build everything (cross-compile for ARM64 inside Docker)
docker compose run build
# → out/Image  out/initramfs.cpio.gz

# 2. Launch QEMU (HVF on Apple Silicon)
./env/run_qemu.sh

# ── Inside QEMU VM ───────────────────────────────────────────────

# 3. One-command demo start (launches tmux, monitor left, harness right)
harness --interactive

# Run from a specific scenario (resume after break)
harness --interactive --start-at D

# Trigger optional Scenario G during Q&A
harness --interactive --scenario G

# Unattended run (CI / recording)
harness --auto --scenario all

# Physical memory inspector (used live during Scenario D)
devmem watch 0x40001000

# Generate comparison table
analysis --output /results/comparison_table.md
```

---

## Optional: Clang XRay Instrumentation

> [!NOTE]
> XRay is **not** part of the default build. It is an optional measurement
> mode that produces per-function timing histograms of the CTX boundary
> enforcement overhead. Enable it only when you need precise profiling data
> for a technical report or backup slides.

### What XRay Measures

| Function | Why It Matters |
| :--- | :--- |
| `sr_authorized_write()` | Total CTX transition round-trip time |
| `set_memory_ro()` | Cost of protecting the vmalloc PTE |
| `protect_linear_map_pte()` | Cost of protecting the linear map PTE |
| `flush_tlb_kernel_range()` | TLB shootdown latency across all CPUs |
| Monitor `mem_poller_` loop body | Verify 100ms polling is jitter-free |
| `pause_for_presenter()` | Verify keypress response is instant |

### Why Not Default

```
Default build uses: aarch64-linux-gnu-g++ (GCC)
XRay requires:     aarch64-linux-gnu-clang++ (LLVM/Clang)

Switching compilers adds Docker build complexity.
For the demo: std::chrono + ktime_get_ns() is sufficient.
For a published technical report: XRay gives precise histograms.
```

### `CMakePresets.json` — XRay Preset

```json
{
  "name": "xray",
  "displayName": "Clang XRay instrumentation (optional)",
  "cacheVariables": {
    "CMAKE_CXX_COMPILER": "aarch64-linux-gnu-clang++",
    "CMAKE_EXE_LINKER_FLAGS": "-fuse-ld=lld",
    "CMAKE_CXX_FLAGS": "-fxray-instrument -fxray-instruction-threshold=50"
  }
}
```

All other presets (`debug`, `asan`, `tsan`, `release`) continue to use GCC
and are unaffected.

### `Dockerfile.builder` — Optional XRay Stage

Add as a separate stage — never part of the default `artifacts` target:

```dockerfile
# ── Optional: XRay userspace build (Clang only) ────────────────
FROM base AS userspace-xray-builder
RUN apt-get update && apt-get install -y \
    clang-16 lld-16 llvm-16 \
    libc6-dev-arm64-cross libstdc++-12-dev-arm64-cross \
    && rm -rf /var/lib/apt/lists/*
COPY userspace/ /demo/userspace/
COPY cmake/aarch64-toolchain-clang.cmake /demo/
RUN cmake -S /demo/userspace -B /demo/build-xray \
          --preset xray \
          -DCMAKE_TOOLCHAIN_FILE=/demo/aarch64-toolchain-clang.cmake \
          -GNinja \
    && ninja -C /demo/build-xray
```

Trigger explicitly:
```bash
docker build --target userspace-xray-builder -t safety-demo-xray .
```

### `cmake/aarch64-toolchain-clang.cmake`

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   clang-16)
set(CMAKE_CXX_COMPILER clang++-16)
set(CMAKE_C_COMPILER_TARGET   aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld" CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

### GitHub Actions — Optional XRay Job

```yaml
# .github/workflows/build.yml
# This job is NOT required for merge — it is informational only
xray-profile:
  runs-on: ubuntu-latest
  if: github.event_name == 'workflow_dispatch'  # manual trigger only
  steps:
    - uses: actions/checkout@v4
    - name: Install LLVM 16
      run: sudo apt-get install -y clang-16 lld-16 llvm-16
    - name: Build with XRay
      run: docker build --target userspace-xray-builder -t safety-demo-xray .
    - name: Run scenarios and collect trace
      run: |
        docker run safety-demo-xray \
          XRAY_OPTIONS="patch_premain=true xray_mode=xray-basic" \
          ./harness --auto --scenario all
    - name: Generate XRay report
      run: |
        llvm-xray-16 account xray-log.* \
          --instr-map=harness \
          --top=10 --sort=sum \
          --format=text > xray-report.txt
        cat xray-report.txt
    - uses: actions/upload-artifact@v4
      with:
        name: xray-report
        path: xray-report.txt
```

### Usage Inside VM (XRay Build)

```bash
# Run scenarios with XRay tracing enabled
XRAY_OPTIONS="patch_premain=true xray_mode=xray-basic" \
  harness --auto --scenario all

# Analyze on host after copying trace file out:
llvm-xray account xray-log.* --instr-map=harness --top=10 --sort=sum
```

### Expected XRay Output (Backup Slide Data)

```
Function                        Calls   Min(ns)  Median(ns)  Max(ns)
sr_authorized_write               120      890      1,240     2,100
set_memory_ro                     240      420        610     1,050
protect_linear_map_pte            120      520        730     1,400
flush_tlb_kernel_range            240      310        480       890
Dashboard::mem_poller_ [body]    4500       80         95       310
pause_for_presenter                 3       80        105       220
```

This table feeds the **overhead** column in the comparison table and provides
precise numbers for any written safety argument document.

---

## Optional: Hardware Performance Counters (Real ARM64 Only)

> [!IMPORTANT]
> Neither libpapi nor perf TMA work meaningfully inside QEMU.
> QEMU does not emulate TLB miss events, pipeline slot counters,
> or the ARM SPE (Statistical Profiling Extension).
> Run Phase 7 only on **real ARM64 hardware** (Raspberry Pi 4,
> NVIDIA Jetson, or any Cortex-A55/A72/A78 board).
> Results feed the overhead column of the comparison table and
> backup slides for the written safety argument.

### Why Three Independent Measurement Methods

```
XRay   (Phase 6) → userspace function timing, Clang-instrumented
libpapi (Phase 7a) → hardware cycle + TLB miss counters, CPU PMU
perf TMA (Phase 7b) → pipeline slot breakdown, microarchitecture view

Three tools, same numbers → stronger safety argument credibility.
```

---

### Phase 7a — libpapi

**Install on ARM64 host:**
```bash
apt-get install libpapi-dev papi-tools
# Verify PMU access:
papi_avail | grep -E 'TLB|CYC|INS|Cache'
```

**Events to measure:**

| PAPI Event | What it captures | QEMU | Real HW |
| :--- | :--- | :--- | :--- |
| `PAPI_TOT_CYC` | CPU cycles (via PMCCNTR_EL0) | ✓ emulated | ✓ |
| `PAPI_TOT_INS` | Instructions retired | ✓ emulated | ✓ |
| `PAPI_TLB_DM` | Data TLB misses | ✗ zero | ✓ |
| `PAPI_L1_DCM` | L1 data cache misses | ✗ zero | ✓ |
| `PAPI_L2_DCM` | L2 data cache misses | ✗ zero | ✓ |

**Integration in `analysis/` (C++20):**

```cpp
// analysis/papi_counter.hpp
// C++ Core Guidelines R.1: RAII wrapper over PAPI event set
class PapiCounter {
    int event_set_{PAPI_NULL};
public:
    PapiCounter(std::initializer_list<int> events);
    ~PapiCounter();            // PAPI_cleanup_eventset + PAPI_destroy_eventset

    void start();              // PAPI_start
    auto stop() -> std::vector<long long>;  // PAPI_stop — no raw arrays
};

// Usage in scenario measurement:
auto counter = PapiCounter{PAPI_TOT_CYC, PAPI_TLB_DM, PAPI_L1_DCM};
counter.start();
sr_authorized_write(0x5AFE1234UL);
auto counts = counter.stop();
// counts[0] = cycles, counts[1] = TLB misses, counts[2] = L1 misses
```

**Expected output on real ARM64 (Cortex-A72):**

```
Function: sr_authorized_write()  [1 call]
  PAPI_TOT_CYC:  ~3,800 cycles  (at 1.5GHz → ~2,530 ns)
  PAPI_TLB_DM:   ~2 misses      (vmalloc PTE + linear map PTE flush)
  PAPI_L1_DCM:   ~4 misses      (PTE walk: pgd/p4d/pud/pmd/pte)

Function: unprotected write (baseline)
  PAPI_TOT_CYC:  ~4 cycles
  PAPI_TLB_DM:   0 misses
  PAPI_L1_DCM:   0 misses

Overhead ratio: ~950x cycles, 2 TLB misses, 4 L1 cache misses per CTX transition
```

This is the most precise available answer to: **"what does CTX enforcement cost?"**

---

### Phase 7b — perf Top-Down Microarchitecture Analysis (TMA)

**ARM TMA requires PMUv3 + SLOTS event (Armv8.4+ or Cortex-A55/A72/A78).**

```bash
# Verify TMA support on real hardware:
perf stat --topdown echo test
# Must show: retiring%, bad_spec%, fe_bound%, be_bound%
# If it shows "not supported" → CPU predates Armv8.4 TMA
```

**Run per scenario:**

```bash
# Scenario B — Mutex baseline (no CTX)
perf stat --topdown -e \
  '{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound}' \
  harness --auto --scenario B

# Scenario D — DMA bypass (CTX active, flush_tlb happening)
perf stat --topdown -e \
  '{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound}' \
  harness --auto --scenario D

# Scenario F — Full CTX + SMMU
perf stat --topdown ... harness --auto --scenario F
```

**Expected TMA breakdown per scenario (real ARM64):**

```
                    Retiring%  Bad Spec%  FE Bound%  BE Bound%
Scenario B (mutex)    78%        4%          6%         12%
Scenario C (PTE hit)  42%        18%         5%         35%    ← fault path
Scenario D (DMA)      75%        4%          6%         15%
Scenario F (SMMU)     70%        5%          6%         19%

Key signals:
  Scenario C: bad_spec spike → branch mispredict on page fault taken path
  Scenario C: be_bound spike → exception handler memory accesses
  Scenario F: be_bound moderate increase → SMMU fault handler overhead
```

**`backend_bound` drill-down (Level 2 TMA):**

```bash
# Memory-bound vs Core-bound breakdown for Scenario F:
perf stat -e \
  'armv8_pmuv3/stall_backend_mem/,armv8_pmuv3/stall_backend/' \
  harness --auto --scenario F
# stall_backend_mem / stall_backend → fraction that is memory bound
# High value confirms: SMMU fault = memory-bound stall, not compute-bound
```

---

### Measurement Cross-Reference Table

All three methods (XRay, PAPI, perf TMA) should agree on the same story:

```
┌──────────────────────────┬──────────────┬─────────────────┬──────────────────┐
│  Measurement             │  XRay        │  libpapi        │  perf TMA        │
├──────────────────────────┼──────────────┼─────────────────┼──────────────────┤
│  sr_authorized_write()   │  ~1,240 ns   │  ~3,800 cycles  │  BE Bound spike  │
│  flush_tlb latency       │  ~480 ns     │  2 TLB misses   │  Memory Bound ↑  │
│  Page fault (scenario C) │  ~2,100 ns   │  4 L1 misses    │  Bad Spec 18%    │
│  Unprotected baseline    │  ~4 ns       │  ~4 cycles      │  Retiring 78%    │
└──────────────────────────┴──────────────┴─────────────────┴──────────────────┘
```

This cross-reference is the strongest possible evidence for the overhead claim
in a safety argument document — three independent measurement methods converging
on the same order-of-magnitude answer.

---

### File Additions for Phase 7

```
userspace/
└── analysis/
    ├── main.cpp                     (existing)
    ├── papi_counter.hpp             [NEW] RAII PAPI event set wrapper
    ├── papi_counter.cpp             [NEW] PAPI_start/stop implementation
    └── tma_runner.sh                [NEW] perf --topdown script per scenario

results/
    ├── comparison_table.md          (existing — overhead column populated)
    ├── xray-report.txt              (Phase 6)
    ├── papi-report.txt              [NEW] PAPI counters per scenario
    └── tma-report.txt               [NEW] perf TMA per scenario
```

---

## Backup Slides (Not Built — Documented Only)

| Slide | Content |
| :--- | :--- |
| Scenario A | Vanilla silent corruption — the baseline |
| Scenario C | CTX Level 1 (set_memory_ro only) — vmalloc alias only |
| Scenario E | E2E CRC vetter — detection latency measurement |
| Scenario G | Mutex metadata attack — if not triggered live |
| Level 3 CTX | Per-CPU context variable architecture diagram |
| Full NVIDIA CTX | TTBR1 switching + compiler plugin diagram |
| XRay histogram | Function timing breakdown (Phase 6) |
| PAPI counters | TLB misses + cycle counts per scenario (Phase 7a) |
| TMA breakdown | Retiring / Bad Spec / BE Bound per scenario (Phase 7b) |
| OpenOnload note | Kernel-bypass analogy: OpenOnload bypasses kernel net stack the way DMA bypasses MMU — same concept, different domain |

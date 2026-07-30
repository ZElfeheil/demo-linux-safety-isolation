# Linux Safety Isolation Demo — Presentation Guide

> **Format:** 15-minute live interactive demo + 15-minute Q&A
> **Audience:** Mixed — embedded/systems engineers + safety architects
> **Machine:** Apple Silicon Mac, QEMU ARM64 in terminal

---

## The Master Analogy

Plant this in the first 2 minutes. Every subsequent scenario refers back to it.

> *"Imagine your safety-critical data is a server rack
> inside a locked room, inside a building.
> Over the next 15 minutes, we will try three ways to protect that server rack —
> and we will break each one.
> By the end, you will understand what it actually takes."*

### The Full Analogy Map

| Kernel Concept | Physical Analogy |
| :--- | :--- |
| Safety memory (`safety_buf`) | Server rack in a room |
| Mutex | Sign-in sheet on the door |
| Thread A / B (cooperative) | Staff who read and follow the sheet |
| Thread C (rogue) | Contractor who never heard of the sheet |
| `set_memory_ro` / hardware PTE | Keycard lock on the door |
| Linear map alias | Fire exit — second door to the same room, no keycard |
| DMA bus write | Forklift that drives through the wall |
| SMMU | Reinforced concrete perimeter wall around the building |
| Mutex metadata attack | Contractor erases your name from the sign-in sheet |

---

## Timing Breakdown

```
00:00 – 02:00   Intro + master analogy
02:00 – 06:00   Scenario B — Mutex + Rogue Thread
06:00 – 10:30   Scenario D — DMA Linear Map Bypass  ← climax
10:30 – 14:00   Scenario F — Full CTX + SMMU
14:00 – 15:00   Comparison table + closing line
15:00 – 30:00   Q&A (Scenario G available on demand)
```

---

## Intro (00:00 – 02:00)

### Speaker Notes

- Open with the terminal already running. QEMU booted, monitor running in left pane.
- Do NOT show code yet. Start with the analogy.

### Slide: The Setup

```
  Your safety-critical system has one job:
  keep a piece of memory correct.

  If that memory is corrupted — brake pressure reads wrong,
  throttle position is wrong, flight surface angle is wrong.

  The question is: how do you actually protect it?

  Let's find out.
```

- Gesture to the monitor dashboard on screen.
- Point out: left pane shows live kernel memory state. Right pane is where the demo runs.
- Show `value_via_vmalloc: 0x5AFE1234 OK` — this is the value we are protecting.

---

## Scenario B — Mutex + Rogue Thread (02:00 – 06:00)

### Analogy

> *"First attempt: a sign-in sheet on the door.
> Two staff members follow it perfectly — only one person inside at a time.
> Then a contractor arrives from a different company.
> Nobody told them about the sign-in sheet."*

### Beat 1 — Setup (show code, load modules) ~1 min

Show on right pane:

```
Loading: mutex_threads.ko (threads A + B)
  Thread A: safety thread — holds mutex, reads memory
  Thread B: cooperative  — holds mutex, restores value

Monitor: value stable at 0x5AFE1234
```

**Say:**
> *"Thread A and Thread B both follow the rules. They use a mutex.
> Only one at a time. The data stays correct."*

Point to monitor: value is green, stable.

### Beat 2 — Question ~30 sec

Monitor freezes on `⏸ AWAITING PRESENTER`.

**Say:**
> *"Now I'm going to load Thread C — a contractor module.
> Thread C has never heard of this mutex.
> Thread A currently holds the mutex right now.*
>
> *Question for the room:
> Thread A holds the mutex. Thread C ignores it.
> Can Thread C corrupt the data?"*

Display choices:
```
  A)  No  — Thread A holds the mutex, Thread C must wait
  B)  Yes — Thread C ignores the mutex entirely
  C)  It depends on thread priority
```

Wait for audience to discuss. Pause. Then press Enter.

### Beat 3 — Reveal ~30 sec

Monitor shows `VIOLATION: data changed WHILE MUTEX HELD`.

```
  via vmalloc: 0xDEADDEAD  ✗ CORRUPT
  mutex owner: safety_thread    ← A still holds it
```

**Say:**
> *"Answer is B.
> Thread A holds the mutex and the data still changed.
> Thread C didn't break any rules — it simply never knew the rules existed."*

### Beat 4 — Explain ~1 min

> *"This is the fundamental limit of a mutex.
> A sign-in sheet only works if everyone reads it.*
>
> *Mutex gives you mutual exclusion between cooperative parties.
> It gives you zero protection against code that doesn't participate.*
>
> *In kernel space, any module can write to any kernel address.
> There is nothing stopping it — except convention."*

### One-liner takeaway

> **"A sign-in sheet only works if everyone reads it."**

---

## Scenario D — DMA Linear Map Bypass (06:00 – 10:30)

### Analogy

> *"Fine. Let's replace the sign-in sheet with a keycard lock.
> Hardware. Nobody gets past this door without swiping their card.*
>
> *Thread C tries the front door — access denied. Excellent.*
>
> *Then Thread C walks around the building.
> There's a fire exit around the back.
> Same room. Same server rack. No keycard reader on this door."*

### Beat 1 — Setup ~1 min

Load modules. Show CTX protection active:

```
Loading: safety_mem.ko (CTX protection ON)
  → set_memory_ro applied to vmalloc alias
  → ARM64 PTE marked PTE_RDONLY
  → linear map PTE also protected (Level 2)

Monitor: ctx_protected: ON
```

**Say:**
> *"We have installed a keycard lock. Hardware page table enforcement.
> Any direct write to this virtual address will trigger a CPU fault.*
>
> *But now watch: bad_driver is going to try a different address —
> a second virtual address that points to the exact same physical page.*
>
> *This is the linear map — a permanent kernel mapping of all physical RAM.
> Every physical page has a second virtual address here.*
>
> *We protected the front door. We never touched the fire exit."*

Show `devmem watch 0x40001000` running live.

### Beat 2 — Question ~30 sec

**Say:**
> *"CTX protection is active. Hardware keycard on the front door.*
>
> *bad_driver is about to write through the fire exit —
> a different virtual address to the same physical page.*
>
> *Is the data protected?"*

```
  A)  Yes — same physical page, same protection applies
  B)  No  — set_memory_ro protected one virtual alias only
  C)  It depends on whether the TLB was flushed
```

### Beat 3 — Reveal ~30 sec

The visual climax. Point to the monitor:

```
  via vmalloc (front door):  0x5AFE1234  ✓  SAFE
  via phys    (fire exit):   0xDEADDEAD  ✗  CORRUPT
```

> *"Same physical page. Two virtual addresses.
> One protected. One not.
> The fire exit was wide open."*

`devmem watch` shows the physical value flip in real time alongside this.

### Beat 4 — Explain ~1 min

> *"`set_memory_ro` modifies one page table entry — the vmalloc virtual mapping.
> The linear map has its own separate entry to the same physical page.
> We never modified that entry.*
>
> *One physical page. Two virtual aliases. One lock. One open fire exit.*
>
> *This is why a naive CTX implementation — one that only protects
> the virtual alias — gives a false sense of security.*
>
> *And this is the most important row in the comparison table."*

### One-liner takeaway

> **"Locking the front door doesn't help if you forgot the fire exit."**

---

## Scenario F — Full CTX + SMMU (10:30 – 14:00)

### Analogy

> *"We locked the front door. We locked the fire exit.
> No human can get in.*
>
> *Then a forklift drives through the wall."*

### Beat 1 — Setup ~1 min

```
Relaunching QEMU with: -machine virt,iommu=smmuv3
Loading: smmu_guard.ko
  → IOMMU domain configured
  → Physical range [0x40001000, 0x40002000) — bus master write: DENIED
```

**Say:**
> *"We now have Level 2 CTX — both virtual aliases locked.*
>
> *But a real DMA device — GPU, network card, DMA controller —
> does not use virtual addresses at all.
> It writes directly to physical addresses on the memory bus.*
>
> *It is a forklift. Page table entries are irrelevant to it.*
>
> *The SMMU sits between the memory bus and physical RAM.
> It is the perimeter wall.
> Nothing crosses it without explicit permission."*

### Beat 2 — Question ~30 sec

**Say:**
> *"Both virtual doors are locked. The SMMU is active.*
>
> *bad_driver simulates a DMA write — directly to the physical address,
> the way a real GPU or DMA controller would.*
>
> *Does the SMMU block a CPU-initiated physical write?"*

```
  A)  Yes — SMMU blocks all physical writes
  B)  No  — SMMU controls bus masters only, not the CPU
  C)  Only with iommu=strict kernel parameter
```

### Beat 3 — Reveal ~30 sec

```
  SMMU fault: BLOCKED — bus master write to protected range
  via vmalloc: 0x5AFE1234  ✓  SAFE
  via phys:    0x5AFE1234  ✓  SAFE
```

> *"Answer B — and this is subtle.*
>
> *The CPU has its own MMU. We handle that with Level 2 PTE protection.
> The SMMU handles the physical bus. They are separate systems.
> You need both."*

### Beat 4 — Explain ~1 min

> *"The complete solution is two layers:*
>
> *CPU MMU: closes all virtual paths.*
> *SMMU: closes the physical bus.*
>
> *Together, neither a thread nor a forklift can reach your server rack.*
>
> *This is what NVIDIA's CTX system is built on.
> This is why it requires not just kernel changes, but SMMU cooperation.
> And this is the open problem ELISA is trying to standardize
> for mainline Linux."*

### One-liner takeaway

> **"Door locks stop people. They don't stop forklifts."**

---

## Comparison Table (14:00 – 15:00)

Run `analysis` in the harness pane. Table appears in terminal:

```
╔═══════════════╦══════════════════════╦══════════════════════╦═══════════╦════════════╗
║  Scenario     ║  Protection          ║  Attack              ║  Data OK? ║  Detected? ║
╠═══════════════╬══════════════════════╬══════════════════════╬═══════════╬════════════╣
║  B Mutex      ║  Sign-in sheet       ║  Contractor          ║  ✗        ║  ✗         ║
║  D CTX only   ║  Front door locked   ║  Fire exit           ║  ✗        ║  ✗         ║
║  F CTX + SMMU ║  Both doors + wall   ║  Forklift            ║  ✓        ║  ✓ SMMU    ║
╚═══════════════╩══════════════════════╩══════════════════════╩═══════════╩════════════╝
```

### Closing Line

> *"Software is the sign-in sheet.
> Hardware page tables are the keycard lock.
> SMMU is the perimeter wall.*
>
> *Safety does not come from convention.
> It comes from hardware that cannot be bypassed
> by the code it is protecting."*

---

## Q&A — Scenario G (On Demand)

Trigger with: `harness --interactive --scenario G`

### When to trigger

Use this if the audience asks:
- *"But what if the bad driver corrupts the mutex itself?"*
- *"Is the mutex object also protected?"*
- *"What about the lock metadata?"*

### Analogy

> *"What if the contractor can't walk past the sign-in sheet
> because staff member A's name is on it?*
>
> *So instead — the contractor picks up a pen and erases A's name.
> Now the sheet says the room is empty."*

### Question

> "A's name is erased. Staff member B checks the sheet —
> sees it's empty — and walks in. A is still inside.
> What happens?"

```
  A)  Nothing — A knows they're inside, B must wait
  B)  Both A and B are inside at the same time    ← CORRECT
  C)  The sheet detects the inconsistency and panics
```

### Reveal + Explanation

> *"Answer B. Two people in the room simultaneously.*
>
> *The sign-in sheet is just paper in the same room.
> Anyone who can reach the room can erase it.*
>
> *The mutex struct is just data in writable kernel memory.
> Any kernel code can overwrite it.*
>
> *This is why you cannot build safety isolation from software alone.
> The protection mechanism itself must be protected by hardware."*

### One-liner takeaway

> **"The sign-in sheet is just paper in the same room."**

---

## Anticipated Q&A

### "Why not SELinux or AppArmor?"

> *"SELinux controls process-level access in userspace.
> Kernel modules run at the same privilege level as the kernel itself.
> SELinux cannot restrict what kernel code does to kernel memory.
> It is a floor — not a ceiling."*

### "Is this solved in real products today?"

> *"NVIDIA's Drive platform uses their CTX system in production — the direct
> inspiration for this demo. QNX achieves isolation via hypervisor — separate
> address spaces per domain. The Linux-native path without a hypervisor
> requires these kernel patches. That is the open problem ELISA is working on."*

### "What is the overhead?"

> *"The comparison table gives measured numbers from this demo.
> SMMU fault handling: roughly 5–20 microseconds per blocked event.
> PTE protection: near-zero runtime overhead after the one-time setup."*

### "Can the SMMU be bypassed?"

> *"A hardware bug in the SMMU could theoretically be exploited.
> SMMUv3 is an ARM-specified standard with formal verification work.
> The safety threat model treats SMMU as a trusted hardware root —
> same status as the CPU itself."*

### "Why does the linear map exist at all?"

> *"Linux needs to access any physical memory at any time —
> for page allocation, DMA buffers, hardware drivers.
> The linear map is a permanent virtual window into all physical RAM.
> Designed for performance and convenience, not isolation.
> CTX must explicitly close this window for safety-critical pages."*

---

## Recovery Procedures

If QEMU hangs or a module panics during the demo:

```bash
# Resume from a specific scenario without rebooting the demo
harness --interactive --start-at D

# Enter QEMU monitor (Ctrl+A then C)
info registers       # CPU state
info mem             # memory mappings
quit                 # kill QEMU

# Relaunch QEMU (kernel + rootfs already built in ./out/)
./env/run_qemu.sh
```

> [!TIP]
> Run 3 cold-boot tests before the presentation.
> Keep `./env/run_qemu.sh` ready to paste if QEMU needs restart.
> `--start-at D` returns to the climax scenario in under 30 seconds.

---

## Backup Slides

| Slide | Trigger |
| :--- | :--- |
| Scenario A — Vanilla silent corruption | *"What does the baseline look like?"* |
| Scenario C — `set_memory_ro` Level 1 only | *"What about the partial CTX approach?"* |
| Scenario E — CRC E2E vetter | *"Can you detect it even if you can't prevent it?"* |
| Level 3 CTX — per-CPU context variable | *"How does the full NVIDIA CTX work?"* |
| NVIDIA CTX diagram — TTBR1 + compiler plugin | *"How does NVIDIA implement it in production?"* |
| ELISA checklist reference | *"What standards framework does this map to?"* |

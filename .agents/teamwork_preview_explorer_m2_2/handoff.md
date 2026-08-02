# Exploration & Design Handoff: Kernel Modules `mutex_threads.c` & `rogue_thread.c`

## 1. Observation

### System & Repository Specifications
- **Target OS / Kernel**: Linux 6.6 LTS on ARM64 (`aarch64-linux-gnu-gcc`, kernel 6.6 tree).
- **Target Architecture**: QEMU ARM64 (`-machine virt,iommu=smmuv3 -cpu cortex-a57 -m 512M`).
- **Project Specifications**: Documented in `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/orchestrator/PROJECT.md` and `docs/implementation_plan.md`.
- **Target Subsystem**: `kernel/mutex_threads/` containing:
  1. `mutex_threads.c` — Spawns Thread A (Safety thread) and Thread B (Cooperative thread).
  2. `rogue_thread.c` — Spawns Thread C (Rogue thread / Contractor), loaded independently as `rogue_thread.ko`.
  3. `Makefile` — Sub-directory Kbuild configuration building both `.ko` artifacts.

### Key Existing Contracts & Code Structures
1. **Shared Safety Memory**:
   - `safety_mem.ko` allocates the physical/vmalloc shared buffer (`safety_buf`) initialized with `SAFETY_SENTINEL = 0x5AFE1234`.
   - `safety_mem.ko` exports symbol `safety_buf_ptr` via `EXPORT_SYMBOL_GPL(safety_buf_ptr);` (or provides `get_safety_buf()` / `get_safety_buf_phys()`).
2. **`/proc/safety_mem_status` Interface**:
   - Requires reporting `mutex_owner` (e.g. `safety_thread`, `coop_thread`, `rogue_thread`, or `none`) and lock contention status alongside `value_via_vmalloc` and `value_via_phys`.
3. **Ftrace Event Pipeline**:
   - Monitored by userspace `Dashboard` via `/sys/kernel/tracing/trace_pipe`.
   - Standard log lines produced via `trace_printk()` for violation detection and thread state transitions.

---

## 2. Logic Chain

### 2.1 Thread Architecture & Lifecycle Strategy

```
                          ┌─────────────────────────────────────────┐
                          │          kernel/safety_mem.ko           │
                          │  Exports: safety_buf_ptr, phys_addr     │
                          └────────────────────┬────────────────────┘
                                               │
                       ┌───────────────────────┴───────────────────────┐
                       ▼                                               ▼
         ┌───────────────────────────┐                   ┌───────────────────────────┐
         │ kernel/mutex_threads.ko   │                   │  kernel/rogue_thread.ko   │
         │                           │                   │                           │
         │  DEFINE_MUTEX(safety_mutex)                   │  Imports: safety_buf_ptr  │
         │  EXPORT_SYMBOL_GPL        │                   │  (Bypasses safety_mutex)  │
         │                           │                   │                           │
         │  Thread A: Safety         │                   │  Thread C: Rogue          │
         │  - mutex_lock             │                   │  - NO mutex_lock          │
         │  - holds lock 50ms        │                   │  - Direct write 0xDEAD    │
         │  - detects violations     │                   │  - Triggers violation      │
         │                           │                   └───────────────────────────┘
         │  Thread B: Cooperative    │
         │  - mutex_lock             │
         │  - restores 0x5AFE        │
         │  - mutex_unlock           │
         └───────────────────────────┘
```

#### Thread A (Safety Thread) Logic:
- **Role**: Validates data consistency under lock ownership.
- **Cycle**:
  1. `mutex_lock(&safety_mutex)`
  2. Reads `u32 initial_val = *safety_buf_ptr;`
  3. Holds lock across a delay window: `msleep_interruptible(hold_duration_ms)` (default 50ms).
  4. Reads `u32 current_val = *safety_buf_ptr;`
  5. **Violation Detection**: If `current_val != initial_val`, an uncooperative writer (Thread C) modified memory while Thread A held the mutex!
  6. Emits ftrace event:
     `trace_printk("VIOLATION: data changed WHILE MUTEX HELD! prev=0x%08x curr=0x%08x by owner=%s\n", initial_val, current_val, current->comm);`
  7. `mutex_unlock(&safety_mutex)`
  8. Sleep until next cycle: `msleep_interruptible(thread_a_period_ms)` (default 500ms).

#### Thread B (Cooperative Thread) Logic:
- **Role**: Simulates a well-behaved secondary task respecting software locking contracts.
- **Cycle**:
  1. `mutex_lock(&safety_mutex)`
  2. Restores buffer state: `*safety_buf_ptr = SAFETY_SENTINEL;` (0x5AFE1234).
  3. `mutex_unlock(&safety_mutex)`
  4. Sleep until next cycle: `msleep_interruptible(thread_b_period_ms)` (default 1000ms).

#### Thread C (Rogue Thread) Logic (`rogue_thread.c`):
- **Role**: Simulates third-party / legacy / rogue kernel code that does not participate in `safety_mutex` locking.
- **Attack Modes**:
  - `attack_mode = 0` (Unsynchronized Write — Scenario B):
    Directly performs `*safety_buf_ptr = 0xDEADDEAD;` WITHOUT acquiring `safety_mutex`.
    Emits `trace_printk("rogue_thread: Rogue Thread C performing unsynchronized write 0x%08x\n", 0xDEADDEAD);`.
  - `attack_mode = 1` (Lock Metadata Tampering — Scenario G):
    Directly manipulates `safety_mutex.owner` (e.g. `WRITE_ONCE(safety_mutex.owner.counter, 0);`) while Thread A holds it, demonstrating that in-RAM lock data structures can be corrupted.

### 2.2 Kernel Thread Lifecycle Management Mechanics
To avoid kernel panics, dangling pointers, or deadlock during `rmmod`:
1. Spawning: `kthread_run()` initializes thread function with descriptive names (`kthread_safety_a`, `kthread_coop_b`, `kthread_rogue_c`).
2. Execution Loop:
   ```c
   while (!kthread_should_stop()) {
       /* logic block */
       msleep_interruptible(period_ms);
   }
   ```
3. Shutdown Protocol:
   - Module `exit` handler sets stopping state and calls `kthread_stop(task_ptr)`.
   - If thread is sleeping in `msleep_interruptible()`, `kthread_stop()` sends a signal to wake it up immediately, ensuring rapid module unloading without system delay.
   - Use `struct completion` if thread synchronization at startup/teardown is required.

### 2.3 Interface Integration with `/proc/safety_mem_status`
To support `/proc/safety_mem_status` reporting:
- `mutex_threads.c` maintains lock status indicators:
  ```c
  struct mutex_status_info {
      bool is_locked;
      pid_t owner_pid;
      char owner_name[TASK_COMM_LEN];
      u32 contention_count;
      u32 violation_count;
  };
  ```
- Export accessor function: `void get_safety_mutex_status(struct mutex_status_info *info);`
- `/proc/safety_mem_status` formats:
  - `mutex_owner: safety_thread` (when Thread A holds lock)
  - `mutex_owner: coop_thread` (when Thread B holds lock)
  - `mutex_owner: none` (when lock is free)

---

## 3. Detailed Technical Blueprint & Source Templates

### 3.1 `kernel/mutex_threads/mutex_threads.h`
```c
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MUTEX_THREADS_H
#define _MUTEX_THREADS_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#define SAFETY_SENTINEL   0x5AFE1234U
#define CORRUPT_SENTINEL  0xDEADDEADU

struct mutex_status_info {
	bool is_locked;
	pid_t owner_pid;
	char owner_name[TASK_COMM_LEN];
	u32 contention_count;
	u32 violation_count;
};

/* Exported interfaces */
extern struct mutex safety_mutex;
void get_safety_mutex_status(struct mutex_status_info *info);

#endif /* _MUTEX_THREADS_H */
```

### 3.2 `kernel/mutex_threads/mutex_threads.c`
```c
// SPDX-License-Identifier: GPL-2.0
/*
 * mutex_threads.c - Safety Thread A and Cooperative Thread B Implementation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>

#include "mutex_threads.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("Kernel module running Thread A (Safety) and Thread B (Cooperative)");
MODULE_VERSION("1.0");

/* Module Parameters */
static unsigned int thread_a_delay_ms = 500;
module_param(thread_a_delay_ms, uint, 0644);
MODULE_PARM_DESC(thread_a_delay_ms, "Thread A loop period in ms (default: 500)");

static unsigned int thread_b_delay_ms = 1000;
module_param(thread_b_delay_ms, uint, 0644);
MODULE_PARM_DESC(thread_b_delay_ms, "Thread B loop period in ms (default: 1000)");

static unsigned int hold_duration_ms = 50;
module_param(hold_duration_ms, uint, 0644);
MODULE_PARM_DESC(hold_duration_ms, "Thread A mutex hold duration in ms (default: 50)");

/* Exported Mutex */
DEFINE_MUTEX(safety_mutex);
EXPORT_SYMBOL_GPL(safety_mutex);

/* External Safety Memory Pointer (from safety_mem.ko) */
extern u32 *safety_buf_ptr;

/* State & Task Tracking */
static struct task_struct *task_a;
static struct task_struct *task_b;

static u32 violation_counter;
static u32 contention_counter;

void get_safety_mutex_status(struct mutex_status_info *info)
{
	if (!info)
		return;

	info->is_locked = mutex_is_locked(&safety_mutex);
	info->contention_count = contention_counter;
	info->violation_count = violation_counter;

	if (info->is_locked) {
		struct task_struct *owner = READ_ONCE(safety_mutex.owner);
		/* Mask lower bits in ARM64/kernel mutex owner struct task_struct pointer */
		owner = (struct task_struct *)((unsigned long)owner & ~0x07UL);
		if (owner) {
			info->owner_pid = owner->pid;
			get_task_comm(info->owner_name, owner);
		} else {
			info->owner_pid = -1;
			strscpy(info->owner_name, "unknown", sizeof(info->owner_name));
		}
	} else {
		info->owner_pid = 0;
		strscpy(info->owner_name, "none", sizeof(info->owner_name));
	}
}
EXPORT_SYMBOL_GPL(get_safety_mutex_status);

static int safety_thread_a_fn(void *data)
{
	pr_info("Thread A (Safety) started\n");
	trace_printk("mutex_threads: Thread A (safety) started\n");

	while (!kthread_should_stop()) {
		u32 initial_val, current_val;

		if (!mutex_trylock(&safety_mutex)) {
			contention_counter++;
			mutex_lock(&safety_mutex);
		}

		if (safety_buf_ptr)
			initial_val = READ_ONCE(*safety_buf_ptr);
		else
			initial_val = 0;

		/* Hold lock to expose window for uncooperative writes */
		msleep_interruptible(hold_duration_ms);

		if (safety_buf_ptr)
			current_val = READ_ONCE(*safety_buf_ptr);
		else
			current_val = 0;

		/* Violation Check */
		if (current_val != initial_val) {
			violation_counter++;
			pr_err("VIOLATION DETECTED: data changed WHILE MUTEX HELD! 0x%08x -> 0x%08x\n",
			       initial_val, current_val);
			trace_printk("VIOLATION: data changed WHILE MUTEX HELD! 0x%08x -> 0x%08x\n",
				     initial_val, current_val);
		}

		mutex_unlock(&safety_mutex);

		msleep_interruptible(thread_a_delay_ms);
	}

	pr_info("Thread A stopping\n");
	return 0;
}

static int coop_thread_b_fn(void *data)
{
	pr_info("Thread B (Cooperative) started\n");
	trace_printk("mutex_threads: Thread B (cooperative) started\n");

	while (!kthread_should_stop()) {
		mutex_lock(&safety_mutex);

		if (safety_buf_ptr) {
			WRITE_ONCE(*safety_buf_ptr, SAFETY_SENTINEL);
			trace_printk("mutex_threads: Thread B restored sentinel 0x%08x\n", SAFETY_SENTINEL);
		}

		mutex_unlock(&safety_mutex);

		msleep_interruptible(thread_b_delay_ms);
	}

	pr_info("Thread B stopping\n");
	return 0;
}

static int __init mutex_threads_init(void)
{
	pr_info("Initializing mutex_threads module\n");

	task_a = kthread_run(safety_thread_a_fn, NULL, "safety_thread");
	if (IS_ERR(task_a)) {
		pr_err("Failed to create Thread A\n");
		return PTR_ERR(task_a);
	}

	task_b = kthread_run(coop_thread_b_fn, NULL, "coop_thread");
	if (IS_ERR(task_b)) {
		pr_err("Failed to create Thread B\n");
		kthread_stop(task_a);
		return PTR_ERR(task_b);
	}

	return 0;
}

static void __exit mutex_threads_exit(void)
{
	pr_info("Cleaning up mutex_threads module\n");
	if (task_a)
		kthread_stop(task_a);
	if (task_b)
		kthread_stop(task_b);
}

module_init(mutex_threads_init);
module_exit(mutex_threads_exit);
```

### 3.3 `kernel/mutex_threads/rogue_thread.c`
```c
// SPDX-License-Identifier: GPL-2.0
/*
 * rogue_thread.c - Thread C (Rogue / Contractor Thread) Implementation
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/ftrace.h>

#include "mutex_threads.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("Rogue Kernel Thread simulating uncooperative memory access");
MODULE_VERSION("1.0");

static unsigned int attack_mode = 0;
module_param(attack_mode, uint, 0644);
MODULE_PARM_DESC(attack_mode, "0: Unsynchronized write, 1: Lock metadata attack (Scenario G)");

static unsigned int corrupt_val = CORRUPT_SENTINEL;
module_param(corrupt_val, uint, 0644);
MODULE_PARM_DESC(corrupt_val, "Value written by rogue thread (default: 0xDEADDEAD)");

static unsigned int interval_ms = 300;
module_param(interval_ms, uint, 0644);
MODULE_PARM_DESC(interval_ms, "Attack write frequency in ms (default: 300)");

extern u32 *safety_buf_ptr;

static struct task_struct *task_c;

static int rogue_thread_c_fn(void *data)
{
	pr_info("Thread C (Rogue) started in attack_mode=%u\n", attack_mode);
	trace_printk("rogue_thread: Rogue Thread C started (attack_mode=%u)\n", attack_mode);

	while (!kthread_should_stop()) {
		if (attack_mode == 0) {
			/* Scenario B: Unsynchronized write bypassing mutex */
			if (safety_buf_ptr) {
				WRITE_ONCE(*safety_buf_ptr, corrupt_val);
				trace_printk("rogue_thread: Unsynchronized write 0x%08x to safety_buf WITHOUT LOCK\n",
					     corrupt_val);
			}
		} else if (attack_mode == 1) {
			/* Scenario G: Lock Metadata Attack - corrupt mutex struct in RAM */
			if (mutex_is_locked(&safety_mutex)) {
				/* Overwrite owner pointer to clear lock state improperly */
				WRITE_ONCE(safety_mutex.owner, NULL);
				trace_printk("rogue_thread: METADATA ATTACK: Cleared mutex.owner while locked!\n");
			}
		}

		msleep_interruptible(interval_ms);
	}

	pr_info("Thread C stopping\n");
	return 0;
}

static int __init rogue_thread_init(void)
{
	pr_info("Initializing rogue_thread module\n");

	task_c = kthread_run(rogue_thread_c_fn, NULL, "rogue_thread");
	if (IS_ERR(task_c)) {
		pr_err("Failed to create Thread C\n");
		return PTR_ERR(task_c);
	}

	return 0;
}

static void __exit rogue_thread_exit(void)
{
	pr_info("Cleaning up rogue_thread module\n");
	if (task_c)
		kthread_stop(task_c);
}

module_init(rogue_thread_init);
module_exit(rogue_thread_exit);
```

### 3.4 `kernel/mutex_threads/Makefile`
```makefile
# SPDX-License-Identifier: GPL-2.0
obj-m += mutex_threads.o
obj-m += rogue_thread.o

ccflags-y := -Wall -Wextra -Werror -Wno-unused-parameter
```

### 3.5 Top-level `kernel/Makefile`
```makefile
# SPDX-License-Identifier: GPL-2.0
obj-m += safety_mem/
obj-m += bad_driver/
obj-m += mutex_threads/
obj-m += ctx_monitor/
obj-m += smmu_guard/

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
```

---

## 4. Caveats & Assumptions

1. **Kernel Symbol Resolution (`Module.symvers`)**:
   - `mutex_threads.ko` depends on `safety_buf_ptr` exported by `safety_mem.ko`. `safety_mem.ko` MUST be loaded before `mutex_threads.ko`.
   - When building out-of-tree modules using Kbuild, Kbuild generates `Module.symvers` in each module directory. To avoid build warnings/errors during module compilation, `KBUILD_EXTRA_SYMBOLS` should point to `safety_mem/Module.symvers` if modules are compiled individually.
2. **ARM64 Mutex Layout Specifics**:
   - In Linux 6.6 on ARM64, `struct mutex` uses fastpath lockless atomic bit manipulations on `lock->owner`. Accessing `owner` directly requires clearing lower flag bits (`~0x07UL`) before dereferencing as a `struct task_struct *`.
3. **No Code Written to Source Directories**:
   - In accordance with Explorer role constraints, no source files have been created in `kernel/`. This blueprint serves as the definitive specification for the Implementer agent.

---

## 5. Conclusion

The designed blueprint for `mutex_threads.c` and `rogue_thread.c` cleanly demonstrates the fundamental limitation of software-only mutual exclusion in kernel space:
- **Thread A (Safety)** holds `safety_mutex` and detects data corruption during its critical section window.
- **Thread B (Cooperative)** respects `safety_mutex` and restores `SAFETY_SENTINEL` (0x5AFE1234).
- **Thread C (Rogue)** runs in an independent module (`rogue_thread.ko`), bypassing `safety_mutex` to inject `0xDEADDEAD` or tamper with lock metadata (Scenario G).
- Standard kernel primitives (`kthread_run`, `kthread_stop`, `DEFINE_MUTEX`, `trace_printk`, `/proc/safety_mem_status` integration) are fully specified and ready for implementation.

---

## 6. Verification Method

To verify the implementation once written:

### 1. Static Analysis Verification
Run Sparse and Smatch inside the Docker container:
```bash
make C=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KERNEL_SRC=/demo/linux-6.6 -C kernel/mutex_threads
```

### 2. Functional Verification inside QEMU
1. **Load Base Memory Module**:
   ```bash
   insmod /modules/safety_mem.ko
   cat /proc/safety_mem_status
   ```
2. **Load Mutex Threads (Threads A & B)**:
   ```bash
   insmod /modules/mutex_threads.ko
   cat /proc/safety_mem_status
   ```
   *Expected Output*: `mutex_owner` alternates between `safety_thread`, `coop_thread`, and `none`. `value_via_vmalloc` remains `0x5AFE1234`.

3. **Load Rogue Thread (Thread C — Scenario B Attack)**:
   ```bash
   insmod /modules/rogue_thread.ko attack_mode=0
   cat /proc/safety_mem_status
   cat /sys/kernel/tracing/trace_pipe | grep VIOLATION
   ```
   *Expected Output*: `trace_pipe` logs `VIOLATION: data changed WHILE MUTEX HELD! 0x5AFE1234 -> 0xDEADDEAD`.

4. **Clean Unload Verification**:
   ```bash
   rmmod rogue_thread
   rmmod mutex_threads
   rmmod safety_mem
   dmesg | tail -n 20
   ```
   *Expected Output*: All threads terminate cleanly without kernel warning or panic.

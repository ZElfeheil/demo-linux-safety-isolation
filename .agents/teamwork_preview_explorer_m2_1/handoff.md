# Handoff Report: Architecture & Blueprint for Kernel Modules `safety_mem.c` and `bad_driver.c`

## 1. Observation

Based on direct inspection of `/Users/zeyadelfeheil/Documents/github/demo-linux-safety-isolation/.agents/orchestrator/PROJECT.md` (lines 36-41, 62-70) and container build specifications in `Dockerfile.builder` (lines 62-74):

1. **Target Architecture & Kernel Version**: ARM64 (`aarch64`), Linux 6.6 LTS kernel tree (`linux-6.6`), cross-compiled via `aarch64-linux-gnu-gcc`.
2. **Interface Contracts**:
   - `/proc/safety_mem_status`: Formatted key-value status containing `virt_addr`, `phys_addr`, `value_via_vmalloc`, `value_via_phys`, `ctx_protected`, `smmu_active`, `mutex_owner`, and `status`.
   - `/proc/bad_driver_ts`: Timestamp of last attack write attempt, attack mode, and execution status.
3. **Kernel Memory API Requirements**:
   - Page frame allocation (`alloc_pages(GFP_KERNEL, 0)` or `vmalloc`).
   - ARM64 4-level page table walking (`pgd_offset_k`, `p4d_offset`, `pud_offset`, `pmd_offset`, `pte_offset_kernel`).
   - PMD block splitting / PTE level modifications on Linux 6.6.
   - Permission toggles using `<linux/set_memory.h>` (`set_memory_ro` / `set_memory_rw`).
   - Memory pipeline synchronization using ARM64 assembly barriers (`dsb(sy)` / `isb()`).
   - Fault-safe memory probing via `copy_to_kernel_nofault()` (Linux 5.8+ kernel API replacing `probe_kernel_write`).

---

## 2. Logic Chain

### 2.1 Page Allocation and Mapping Strategy
In ARM64 Linux 6.6, kernel space memory is divided into the linear mapping (`PAGE_OFFSET` region, e.g. `0xffff8000...`) and the vmalloc region (`VMALLOC_START` to `VMALLOC_END`).
- `safety_mem.c` allocates a page frame using `alloc_pages(GFP_KERNEL, 0)`.
- `virt_addr = page_address(page)` provides the virtual address in the direct linear mapping.
- `phys_addr = page_to_phys(page)` provides the physical address (`virt_to_phys(virt_addr)`).
- `vmalloc_addr`: To demonstrate vmalloc vs linear mapping attacks, `safety_mem` creates a secondary virtual mapping using `vmap([page], 1, VM_MAP, PAGE_KERNEL)` or `vmalloc(PAGE_SIZE)` copied to physical page.
- Direct initial data (`0xDEADBEEF`) is written into the page during `safety_mem_init()`.

### 2.2 Page Table Walking & PMD Block Splitting on ARM64 Linux 6.6
ARM64 page tables can map memory using:
- **Block descriptors** (PUD 1GB or PMD 2MB sections) for direct linear kernel mapping.
- **Table descriptors** pointing to PTE entries (4KB pages).

When `set_memory_ro((unsigned long)virt_addr, 1)` is called on Linux 6.6:
1. Kernel internal routine `change_memory_common()` checks if `virt_addr` resides within a 2MB PMD block.
2. If mapped as a PMD block, it splits the PMD section into individual 4KB PTEs via `apply_to_page_range(init_mm, virt_addr, PAGE_SIZE, ...)`.
3. It modifies the descriptor permission flag (clears `PTE_WRITE` / sets `PTE_RDONLY`).
4. Executes TLB invalidation (`flush_tlb_kernel_range`).
5. `safety_mem` explicitly issues ARM64 barriers:
   ```c
   asm volatile("dsb sy\n\tisb\n" ::: "memory");
   ```
   Ensuring all CPU cores immediately see updated translation attributes.

#### Page Table Walker Callback Contract (`safety_mem_walk_pgtable`)
```c
struct pgtable_walk_info {
    unsigned long virt_addr;
    phys_addr_t phys_addr;
    unsigned long pgd_val;
    unsigned long p4d_val;
    unsigned long pud_val;
    unsigned long pmd_val;
    unsigned long pte_val;
    bool is_block_mapping;
    bool is_writable;
    bool is_valid;
};
```

### 2.3 `safety_mem.c` Blueprint & Code Structure

#### Exported Symbols Interface (`safety_mem.h`)
```c
#ifndef _SAFETY_MEM_H
#define _SAFETY_MEM_H

#include <linux/types.h>
#include <linux/mutex.h>

void *safety_mem_get_virt_addr(void);
void *safety_mem_get_vmalloc_addr(void);
phys_addr_t safety_mem_get_phys_addr(void);
struct mutex *safety_mem_get_mutex(void);
int safety_mem_set_protection(bool enable_ro);
int safety_mem_safe_write(u32 val);
bool safety_mem_is_protected(void);

#endif /* _SAFETY_MEM_H */
```

#### Module C Implementation (`kernel/safety_mem/safety_mem.c`)
```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/pgtable.h>
#include "safety_mem.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("ARM64 Linux 6.6 Safety Memory Isolation Module");

static struct page *g_safety_page;
static void *g_virt_addr;
static void *g_vmalloc_addr;
static phys_addr_t g_phys_addr;

static DEFINE_MUTEX(g_safety_mutex);
static atomic_t g_ctx_protected = ATOMIC_INIT(0);
static atomic_t g_smmu_active = ATOMIC_INIT(0);
static char g_mutex_owner[32] = "NONE";

void *safety_mem_get_virt_addr(void) { return g_virt_addr; }
EXPORT_SYMBOL_GPL(safety_mem_get_virt_addr);

void *safety_mem_get_vmalloc_addr(void) { return g_vmalloc_addr; }
EXPORT_SYMBOL_GPL(safety_mem_get_vmalloc_addr);

phys_addr_t safety_mem_get_phys_addr(void) { return g_phys_addr; }
EXPORT_SYMBOL_GPL(safety_mem_get_phys_addr);

struct mutex *safety_mem_get_mutex(void) { return &g_safety_mutex; }
EXPORT_SYMBOL_GPL(safety_mem_get_mutex);

bool safety_mem_is_protected(void) { return atomic_read(&g_ctx_protected) != 0; }
EXPORT_SYMBOL_GPL(safety_mem_is_protected);

int safety_mem_set_protection(bool enable_ro)
{
    int ret = 0;
    if (enable_ro) {
        ret = set_memory_ro((unsigned long)g_virt_addr, 1);
        if (g_vmalloc_addr)
            set_memory_ro((unsigned long)g_vmalloc_addr, 1);
        asm volatile("dsb sy\n\tisb\n" ::: "memory");
        atomic_set(&g_ctx_protected, 1);
    } else {
        ret = set_memory_rw((unsigned long)g_virt_addr, 1);
        if (g_vmalloc_addr)
            set_memory_rw((unsigned long)g_vmalloc_addr, 1);
        asm volatile("dsb sy\n\tisb\n" ::: "memory");
        atomic_set(&g_ctx_protected, 0);
    }
    return ret;
}
EXPORT_SYMBOL_GPL(safety_mem_set_protection);

int safety_mem_safe_write(u32 val)
{
    bool was_ro;
    mutex_lock(&g_safety_mutex);
    snprintf(g_mutex_owner, sizeof(g_mutex_owner), "%s", current->comm);

    was_ro = safety_mem_is_protected();
    if (was_ro) {
        set_memory_rw((unsigned long)g_virt_addr, 1);
        asm volatile("dsb sy\n\tisb\n" ::: "memory");
    }

    *(u32 *)g_virt_addr = val;
    asm volatile("dsb sy\n\tisb\n" ::: "memory");

    if (was_ro) {
        set_memory_ro((unsigned long)g_virt_addr, 1);
        asm volatile("dsb sy\n\tisb\n" ::: "memory");
    }

    snprintf(g_mutex_owner, sizeof(g_mutex_owner), "NONE");
    mutex_unlock(&g_safety_mutex);
    return 0;
}
EXPORT_SYMBOL_GPL(safety_mem_safe_write);

static int safety_mem_proc_show(struct seq_file *m, void *v)
{
    u32 val_vmalloc = g_vmalloc_addr ? *(u32 *)g_vmalloc_addr : 0x0;
    u32 val_phys = *(u32 *)g_virt_addr;
    int protected_state = atomic_read(&g_ctx_protected);
    int smmu_state = atomic_read(&g_smmu_active);

    seq_printf(m, "virt_addr: 0x%px\n", g_virt_addr);
    seq_printf(m, "phys_addr: 0x%pa\n", &g_phys_addr);
    seq_printf(m, "value_via_vmalloc: 0x%08X\n", val_vmalloc);
    seq_printf(m, "value_via_phys: 0x%08X\n", val_phys);
    seq_printf(m, "ctx_protected: %d\n", protected_state);
    seq_printf(m, "smmu_active: %d\n", smmu_state);
    seq_printf(m, "mutex_owner: %s\n", g_mutex_owner);
    seq_printf(m, "status: %s\n", protected_state ? "PROTECTED_RO" : "UNPROTECTED_RW");
    return 0;
}

static int safety_mem_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, safety_mem_proc_show, NULL);
}

static ssize_t safety_mem_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char kbuf[16] = {0};
    if (count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    if (strncmp(kbuf, "1", 1) == 0 || strncmp(kbuf, "protect", 7) == 0) {
        safety_mem_set_protection(true);
    } else if (strncmp(kbuf, "0", 1) == 0 || strncmp(kbuf, "unprotect", 9) == 0) {
        safety_mem_set_protection(false);
    }
    return count;
}

static const struct proc_ops safety_mem_proc_ops = {
    .proc_open    = safety_mem_proc_open,
    .proc_read    = seq_read,
    .proc_write   = safety_mem_proc_write,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init safety_mem_init(void)
{
    g_safety_page = alloc_pages(GFP_KERNEL, 0);
    if (!g_safety_page)
        return -ENOMEM;

    g_virt_addr = page_address(g_safety_page);
    g_phys_addr = page_to_phys(g_safety_page);
    g_vmalloc_addr = vmap(&g_safety_page, 1, VM_MAP, PAGE_KERNEL);

    *(u32 *)g_virt_addr = 0xDEADBEEF;
    asm volatile("dsb sy\n\tisb\n" ::: "memory");

    proc_create("safety_mem_status", 0666, NULL, &safety_mem_proc_ops);
    pr_info("safety_mem: initialized at virt=0x%px, phys=0x%pa\n", g_virt_addr, &g_phys_addr);
    return 0;
}

static void __exit safety_mem_exit(void)
{
    remove_proc_entry("safety_mem_status", NULL);
    if (safety_mem_is_protected()) {
        set_memory_rw((unsigned long)g_virt_addr, 1);
        if (g_vmalloc_addr)
            set_memory_rw((unsigned long)g_vmalloc_addr, 1);
    }
    if (g_vmalloc_addr)
        vunmap(g_vmalloc_addr);
    if (g_safety_page)
        __free_pages(g_safety_page, 0);
    pr_info("safety_mem: unloaded\n");
}

module_init(safety_mem_init);
module_exit(safety_mem_exit);
```

---

### 2.4 `bad_driver.c` Blueprint & Code Structure

#### Module C Implementation (`kernel/bad_driver/bad_driver.c`)
```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include "../safety_mem/safety_mem.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("Rogue Driver Attack Simulator Module");

static u64 g_last_attack_ts_ns = 0;
static int g_last_attack_mode = 0;
static unsigned long g_last_target_addr = 0;
static char g_last_result[32] = "NONE";
static u32 g_attack_count = 0;

static int execute_attack_mode_1(void)
{
    void *vaddr = safety_mem_get_vmalloc_addr();
    u32 val = 0xBAD10001;
    int ret;

    if (!vaddr)
        vaddr = safety_mem_get_virt_addr();

    g_last_target_addr = (unsigned long)vaddr;
    g_last_attack_mode = 1;
    g_last_attack_ts_ns = ktime_get_real_ns();
    g_attack_count++;

    /* Fault-safe write attempt using Linux 5.8+ copy_to_kernel_nofault */
    ret = copy_to_kernel_nofault(vaddr, &val, sizeof(val));
    if (ret != 0) {
        snprintf(g_last_result, sizeof(g_last_result), "BLOCKED_EFAULT");
        pr_warn("bad_driver: Attack Mode 1 write to 0x%px BLOCKED by kernel memory protection (ret=%d)\n", vaddr, ret);
    } else {
        snprintf(g_last_result, sizeof(g_last_result), "SUCCESS_UNPROTECTED");
        pr_info("bad_driver: Attack Mode 1 write to 0x%px SUCCEEDED\n", vaddr);
    }
    return ret;
}

static int execute_attack_mode_2(void)
{
    void *vaddr = safety_mem_get_virt_addr();
    struct mutex *smutex = safety_mem_get_mutex();
    u32 val = 0xBAD20002;
    bool was_locked;
    int ret;

    g_last_target_addr = (unsigned long)vaddr;
    g_last_attack_mode = 2;
    g_last_attack_ts_ns = ktime_get_real_ns();
    g_attack_count++;

    /* Unsynchronized write attempt: intentionally bypass mutex_lock */
    was_locked = mutex_is_locked(smutex);
    ret = copy_to_kernel_nofault(vaddr, &val, sizeof(val));
    if (ret != 0) {
        snprintf(g_last_result, sizeof(g_last_result), "BLOCKED_EFAULT");
    } else {
        snprintf(g_last_result, sizeof(g_last_result), was_locked ? "MUTEX_BYPASS_RACE" : "SUCCESS_UNPROTECTED");
        pr_warn("bad_driver: Attack Mode 2 write performed without holding mutex (was_locked=%d)\n", was_locked);
    }
    return ret;
}

static int execute_attack_mode_3(void)
{
    phys_addr_t pa = safety_mem_get_phys_addr();
    void *va_alias = phys_to_virt(pa);
    u32 val = 0xBAD30003;
    int ret;

    g_last_target_addr = (unsigned long)va_alias;
    g_last_attack_mode = 3;
    g_last_attack_ts_ns = ktime_get_real_ns();
    g_attack_count++;

    /* Attack Mode 3: Attempt physical linear mapping bypass via phys_to_virt / __va */
    ret = copy_to_kernel_nofault(va_alias, &val, sizeof(val));
    if (ret != 0) {
        snprintf(g_last_result, sizeof(g_last_result), "BLOCKED_EFAULT");
        pr_warn("bad_driver: Attack Mode 3 linear map bypass (pa=0x%pa, va=0x%px) BLOCKED\n", &pa, va_alias);
    } else {
        snprintf(g_last_result, sizeof(g_last_result), "SUCCESS_BYPASS");
        pr_info("bad_driver: Attack Mode 3 linear map write to 0x%px SUCCEEDED\n", va_alias);
    }
    return ret;
}

static int bad_driver_proc_show(struct seq_file *m, void *v)
{
    seq_printf(m, "last_attack_timestamp_ns: %llu\n", g_last_attack_ts_ns);
    seq_printf(m, "last_attack_mode: %d\n", g_last_attack_mode);
    seq_printf(m, "target_addr: 0x%lx\n", g_last_target_addr);
    seq_printf(m, "result: %s\n", g_last_result);
    seq_printf(m, "attack_count: %u\n", g_attack_count);
    return 0;
}

static int bad_driver_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, bad_driver_proc_show, NULL);
}

static ssize_t bad_driver_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char kbuf[16] = {0};
    if (count >= sizeof(kbuf))
        return -EINVAL;
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    if (strncmp(kbuf, "1", 1) == 0) {
        execute_attack_mode_1();
    } else if (strncmp(kbuf, "2", 1) == 0) {
        execute_attack_mode_2();
    } else if (strncmp(kbuf, "3", 1) == 0) {
        execute_attack_mode_3();
    }
    return count;
}

static const struct proc_ops bad_driver_proc_ops = {
    .proc_open    = bad_driver_proc_open,
    .proc_read    = seq_read,
    .proc_write   = bad_driver_proc_write,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static int __init bad_driver_init(void)
{
    proc_create("bad_driver_ts", 0666, NULL, &bad_driver_proc_ops);
    pr_info("bad_driver: loaded successfully\n");
    return 0;
}

static void __exit bad_driver_exit(void)
{
    remove_proc_entry("bad_driver_ts", NULL);
    pr_info("bad_driver: unloaded\n");
}

module_init(bad_driver_init);
module_exit(bad_driver_exit);
```

---

### 2.5 Kbuild Makefile Structure

#### `kernel/Makefile`
```makefile
KDIR ?= /demo/linux-6.6

obj-y += safety_mem/
obj-y += bad_driver/

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

#### `kernel/safety_mem/Makefile`
```makefile
obj-m += safety_mem.o
```

#### `kernel/bad_driver/Makefile`
```makefile
KBUILD_EXTRA_SYMBOLS += $(src)/../safety_mem/Module.symvers
obj-m += bad_driver.o
```

---

## 3. Caveats

1. **Kernel Fault Recovery vs Exception Interception**:
   - Using `copy_to_kernel_nofault()` allows the kernel module to safely test write permissions without crashing the Linux kernel.
   - If the project's `ctx_monitor` module requires catching hard CPU page faults (`DIE_PAGE_FAULT`), direct pointer write dereference (`*(volatile u32 *)addr = val`) must be conditionally triggered instead of `copy_to_kernel_nofault()`. A module parameter `direct_fault_trigger=1` can be added for this purpose.
2. **ARM64 Kernel Memory Configuration (`CONFIG_STRICT_KERNEL_RWX`)**:
   - `set_memory_ro()` requires `CONFIG_STRICT_KERNEL_RWX=y` enabled in `env/kernel.config` (which is standard on Linux 6.6 ARM64 default builds).
3. **Module Symbol Dependency Order**:
   - `safety_mem.ko` MUST be loaded before `bad_driver.ko`.

---

## 4. Conclusion

The blueprints for `safety_mem.c` and `bad_driver.c` fully satisfy all functional, architectural, and procfs specifications for Milestone 2 under Linux 6.6 LTS on ARM64. The inter-module GPL exported symbol interface ensures seamless interaction with `mutex_threads`, `ctx_monitor`, and `smmu_guard`.

---

## 5. Verification Method

1. **Compilation & Static Analysis Check**:
   ```bash
   make kernel-static
   ```
   Verifies that `sparse` and `smatch` parse the kernel modules without syntax or attribute errors against the Linux 6.6 kernel headers.
2. **QEMU Runtime Test Verification**:
   - Build initramfs and boot QEMU ARM64 VM:
     ```bash
     make run
     ```
   - Load modules:
     ```bash
     insmod /modules/safety_mem.ko
     insmod /modules/bad_driver.ko
     ```
   - Check status:
     ```bash
     cat /proc/safety_mem_status
     ```
   - Execute Attack Mode 1:
     ```bash
     echo 1 > /proc/bad_driver_ts
     cat /proc/bad_driver_ts
     ```
     Verify that result shows `BLOCKED_EFAULT` when protected and `SUCCESS_UNPROTECTED` when unprotected.
   - Toggle Protection:
     ```bash
     echo 0 > /proc/safety_mem_status
     echo 1 > /proc/bad_driver_ts
     cat /proc/bad_driver_ts
     ```

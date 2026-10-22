// SPDX-License-Identifier: GPL-2.0
/*
 * safety_mem.c - ARM64 Linux 6.6 Safety Memory Allocation & Isolation Module
 *
 * Allocates physical page, manages linear/vmalloc virtual mappings,
 * handles PMD block splitting and PTE permission toggling, and provides
 * /proc/safety_mem_status interface.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
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
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <asm/barrier.h>
#include <asm/pgtable.h>

#include "safety_mem.h"
#include "../demo_constants.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("ARM64 Linux 6.6 Safety Memory Isolation Module");
MODULE_VERSION("1.0");
MODULE_SOFTDEP("pre: ctx_monitor");

/* Global Memory References & Mutex */
static struct page *g_safety_page;
static void *g_virt_addr;
static void *g_vmalloc_addr;
static phys_addr_t g_phys_addr;

u32 *safety_buf_ptr = NULL;
EXPORT_SYMBOL_GPL(safety_buf_ptr);

DEFINE_MUTEX(safety_mutex);
EXPORT_SYMBOL_GPL(safety_mutex);

extern void ctx_monitor_set_protected_range(unsigned long start, unsigned long end);

static atomic_t g_ctx_protected = ATOMIC_INIT(0);
static atomic_t g_smmu_active = ATOMIC_INIT(0);

/** Record SMMU guard availability for the userspace status view. */
void safety_mem_set_smmu_active(bool active)
{
	atomic_set(&g_smmu_active, active ? 1 : 0);
}
EXPORT_SYMBOL_GPL(safety_mem_set_smmu_active);

/* Exported Accessors */
/** @brief Return the direct kernel virtual address of the safety page. */
void *safety_mem_get_virt_addr(void)
{
	return g_virt_addr;
}
EXPORT_SYMBOL_GPL(safety_mem_get_virt_addr);

/** @brief Return the vmalloc alias of the safety page. */
void *safety_mem_get_vmalloc_addr(void)
{
	return g_vmalloc_addr;
}
EXPORT_SYMBOL_GPL(safety_mem_get_vmalloc_addr);

/** @brief Return the physical address backing the safety page. */
phys_addr_t safety_mem_get_phys_addr(void)
{
	return g_phys_addr;
}
EXPORT_SYMBOL_GPL(safety_mem_get_phys_addr);

/** @brief Return the mutex protecting cooperative accesses. */
struct mutex *safety_mem_get_mutex(void)
{
	return &safety_mutex;
}
EXPORT_SYMBOL_GPL(safety_mem_get_mutex);

/** @brief Report whether the safety page is configured read-only. */
bool safety_mem_is_protected(void)
{
	return atomic_read(&g_ctx_protected) != 0;
}
EXPORT_SYMBOL_GPL(safety_mem_is_protected);

/* Page Table Walker Inspection Routine */
struct pgtable_walk_info {
	unsigned long virt_addr;
	phys_addr_t phys_addr;
	pgd_t pgd_val;
	p4d_t p4d_val;
	pud_t pud_val;
	pmd_t pmd_val;
	pte_t pte_val;
	bool is_block_mapping;
	bool is_writable;
	bool is_valid;
};

/* Baseline captured before a protection operation can split a block mapping
 * or change its write permission. */
static struct pgtable_walk_info g_initial_mapping;

static u64 safety_mem_mapping_entry(const struct pgtable_walk_info *info)
{
	return info->is_block_mapping ? pmd_val(info->pmd_val) : pte_val(info->pte_val);
}

static const char *safety_mem_mapping_level(const struct pgtable_walk_info *info)
{
	return info->is_block_mapping ? "PMD_BLOCK" : "PTE_PAGE";
}

/** @brief Inspect the ARM64 page-table levels covering an address. */
static void safety_mem_walk_pgtable(unsigned long addr, struct pgtable_walk_info *info)
{
	memset(info, 0, sizeof(*info));
	info->virt_addr = addr;

	/* The *_offset helpers walk the kernel's ARM64 translation hierarchy:
	 * PGD -> P4D -> PUD -> PMD and, for page mappings, PTE. */
	pgd_t *pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return;
	info->pgd_val = *pgd;

	p4d_t *p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d) || p4d_bad(*p4d))
		return;
	info->p4d_val = *p4d;

	pud_t *pud = pud_offset(p4d, addr);
	if (pud_none(*pud) || pud_bad(*pud))
		return;
	info->pud_val = *pud;

	pmd_t *pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return;
	info->pmd_val = *pmd;

	if (pmd_sect(*pmd)) {
		/* A section PMD maps a large block directly, so no PTE exists. */
		info->is_block_mapping = true;
		info->is_valid = true;
		info->is_writable = pmd_write(*pmd);
		info->phys_addr = (phys_addr_t)pmd_pfn(*pmd) << PAGE_SHIFT;
		return;
	}

	/* pte_offset_kernel() locates the final page-table entry for a normal
	 * kernel page mapping. */
	pte_t *pte = pte_offset_kernel(pmd, addr);
	if (!pte || pte_none(*pte))
		return;
	info->pte_val = *pte;
	info->is_block_mapping = false;
	info->is_valid = pte_present(*pte);
	info->is_writable = pte_write(*pte);
	info->phys_addr = (phys_addr_t)pte_pfn(*pte) << PAGE_SHIFT;
}

/**
 * @brief Apply or remove read-only protection from safety-page mappings.
 * @param enable_ro true to protect both relevant CPU mappings; false to unprotect.
 * @return 0 when all mappings were updated, otherwise a negative errno.
 */
int safety_mem_set_protection(bool enable_ro)
{
	int ret_virt;
	int ret_vmalloc = 0;
	struct pgtable_walk_info virt_mapping;
	struct pgtable_walk_info vmalloc_mapping;

	/* Serialize authorized permission changes and writes using the shared mutex. */
	mutex_lock(&safety_mutex);

	if (enable_ro) {
		ctx_monitor_set_protected_range((unsigned long)g_virt_addr,
						 (unsigned long)g_virt_addr + PAGE_SIZE);
		pr_info("Enabling RO protection on 0x%px (vmalloc: 0x%px)\n",
			g_virt_addr, g_vmalloc_addr);
		/* set_memory_ro() asks the architecture code to clear write permission
		 * for one page at each CPU-visible virtual alias. */
		ret_virt = set_memory_ro((unsigned long)g_virt_addr, 1);
		if (ret_virt)
			pr_warn("set_memory_ro direct alias unavailable: %d\n", ret_virt);
		if (g_vmalloc_addr) {
			ret_vmalloc = set_memory_ro((unsigned long)g_vmalloc_addr, 1);
			if (ret_vmalloc)
				pr_err("set_memory_ro vmalloc alias failed: %d\n", ret_vmalloc);
		}
		/* DSB completes page-table writes; ISB makes following instructions use
		 * the updated translation and permission state. */
		asm volatile("dsb sy\n\tisb\n" ::: "memory");
		safety_mem_walk_pgtable((unsigned long)g_virt_addr, &virt_mapping);
		safety_mem_walk_pgtable((unsigned long)g_vmalloc_addr, &vmalloc_mapping);
		/* The attack path in Scenario F targets the vmalloc alias. Only report
		 * protection enabled after its PTE is verified read-only. */
		atomic_set(&g_ctx_protected,
			   ret_vmalloc == 0 && vmalloc_mapping.is_valid && !vmalloc_mapping.is_writable);
		if (!atomic_read(&g_ctx_protected))
			pr_err("CPU protection verification failed: direct_rw=%d vmalloc_rw=%d\n",
			       virt_mapping.is_writable, vmalloc_mapping.is_writable);
	} else {
		ctx_monitor_set_protected_range(0, 0);
		pr_info("Disabling RO protection (setting RW) on 0x%px\n", g_virt_addr);
		/* Restore writable permissions on both aliases before normal access. */
		ret_virt = set_memory_rw((unsigned long)g_virt_addr, 1);
		if (ret_virt)
			pr_warn("set_memory_rw direct alias unavailable: %d\n", ret_virt);
		if (g_vmalloc_addr) {
			ret_vmalloc = set_memory_rw((unsigned long)g_vmalloc_addr, 1);
			if (ret_vmalloc)
				pr_err("set_memory_rw vmalloc alias failed: %d\n", ret_vmalloc);
		}
		asm volatile("dsb sy\n\tisb\n" ::: "memory");
		atomic_set(&g_ctx_protected, 0);
	}

	mutex_unlock(&safety_mutex);

	safety_mem_walk_pgtable((unsigned long)g_virt_addr, &virt_mapping);
	safety_mem_walk_pgtable((unsigned long)g_vmalloc_addr, &vmalloc_mapping);
	pr_info("PTE walk after protection toggle: direct_rw=%d vmalloc_rw=%d\n",
		virt_mapping.is_writable, vmalloc_mapping.is_writable);

	return ret_vmalloc ? ret_vmalloc : ret_virt;
}
EXPORT_SYMBOL_GPL(safety_mem_set_protection);

/**
 * @brief Write a value through the module's authorized access path.
 * @param val Value to store in the safety buffer.
 * @return 0 when the authorized write completes, otherwise a negative errno.
 */
int safety_mem_safe_write(u32 val)
{
	bool was_ro;

	/* This is the cooperative write path: own the mutex before changing either
	 * permissions or data. */
	mutex_lock(&safety_mutex);

	was_ro = safety_mem_is_protected();
	if (was_ro) {
		set_memory_rw((unsigned long)g_virt_addr, 1);
		if (g_vmalloc_addr)
			set_memory_rw((unsigned long)g_vmalloc_addr, 1);
		asm volatile("dsb sy\n\tisb\n" ::: "memory");
	}

	if (g_virt_addr)
		*(u32 *)g_virt_addr = val;

	asm volatile("dsb sy\n\tisb\n" ::: "memory");

	if (was_ro) {
		set_memory_ro((unsigned long)g_virt_addr, 1);
		if (g_vmalloc_addr)
			set_memory_ro((unsigned long)g_vmalloc_addr, 1);
		asm volatile("dsb sy\n\tisb\n" ::: "memory");
	}

	mutex_unlock(&safety_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(safety_mem_safe_write);

/**
 * @brief Render safety-memory state for the procfs monitor.
 * @param m Output sequence file supplied by seq_file.
 * @param v Iterator-private value required by the seq_file callback API.
 * @return 0 after the current state has been written.
 */
static int safety_mem_proc_show(struct seq_file *m, void *v)
{
	u32 val_vmalloc = 0;
	u32 val_phys = 0;
	struct pgtable_walk_info current_mapping;
	struct pgtable_walk_info vmalloc_mapping;
	int protected_state = atomic_read(&g_ctx_protected);
	int smmu_state = atomic_read(&g_smmu_active);
	char owner_buf[TASK_COMM_LEN] = "none";

	if (g_vmalloc_addr) {
		/* copy_from_kernel_nofault() reads diagnostic data without turning an
		 * inaccessible mapping into a fatal kernel fault. */
		copy_from_kernel_nofault(&val_vmalloc, g_vmalloc_addr, sizeof(val_vmalloc));
	}

	if (g_virt_addr) {
		copy_from_kernel_nofault(&val_phys, g_virt_addr, sizeof(val_phys));
	}

	/* mutex_is_locked() reports state only; it does not acquire the mutex. */
	if (mutex_is_locked(&safety_mutex)) {
		unsigned long owner_val = (unsigned long)atomic_long_read(&safety_mutex.owner);
		struct task_struct *owner =
			(struct task_struct *)(owner_val & ~DEMO_MUTEX_OWNER_FLAGS_MASK);
		if (owner)
			get_task_comm(owner_buf, owner);
		else
			strscpy(owner_buf, "unknown", sizeof(owner_buf));
	}

	safety_mem_walk_pgtable((unsigned long)g_virt_addr, &current_mapping);
	safety_mem_walk_pgtable((unsigned long)g_vmalloc_addr, &vmalloc_mapping);

	seq_printf(m, "virt_addr: 0x%px\n", g_virt_addr);
	seq_printf(m, "phys_addr: %pa\n", &g_phys_addr);
	seq_printf(m, "mapping_level_before: %s\n", safety_mem_mapping_level(&g_initial_mapping));
	seq_printf(m, "mapping_entry_before: 0x%016llX\n",
		   (unsigned long long)safety_mem_mapping_entry(&g_initial_mapping));
	seq_printf(m, "mapping_writable_before: %d\n", g_initial_mapping.is_writable);
	seq_printf(m, "mapping_level_current: %s\n", safety_mem_mapping_level(&current_mapping));
	seq_printf(m, "mapping_entry_current: 0x%016llX\n",
		   (unsigned long long)safety_mem_mapping_entry(&current_mapping));
	seq_printf(m, "mapping_writable_current: %d\n", current_mapping.is_writable);
	seq_printf(m, "mapping_valid_current: %d\n", current_mapping.is_valid);
	seq_printf(m, "mapping_level_vmalloc: %s\n", safety_mem_mapping_level(&vmalloc_mapping));
	seq_printf(m, "mapping_entry_vmalloc: 0x%016llX\n",
		   (unsigned long long)safety_mem_mapping_entry(&vmalloc_mapping));
	seq_printf(m, "mapping_writable_vmalloc: %d\n", vmalloc_mapping.is_writable);
	seq_printf(m, "value_via_vmalloc: 0x%08X\n", val_vmalloc);
	seq_printf(m, "value_via_phys: 0x%08X\n", val_phys);
	seq_printf(m, "ctx_protected: %d\n", protected_state);
	seq_printf(m, "smmu_active: %d\n", smmu_state);
	seq_printf(m, "mutex_owner: %s\n", owner_buf);
	seq_printf(m, "status: %s\n", protected_state ? "PROTECTED_RO" : "UNPROTECTED_RW");

	return 0;
}

/**
 * @brief Open the safety-memory procfs status endpoint.
 * @param inode Procfs inode associated with the endpoint.
 * @param file Open file state populated by single_open.
 * @return 0 on success, or the single_open error code.
 */
static int safety_mem_proc_open(struct inode *inode, struct file *file)
{
	/* single_open() binds one seq_file read operation to proc_show(). */
	return single_open(file, safety_mem_proc_show, NULL);
}

/**
 * @brief Handle commands written to the safety-memory procfs endpoint.
 * @param file Open procfs file state; unused after validation.
 * @param buf User-space command buffer.
 * @param count Number of bytes supplied by user space.
 * @param ppos File position supplied by procfs.
 * @return Number of bytes consumed, or a negative errno on copy/parse failure.
 *
 * Supported commands toggle protection and perform the authorized write path;
 * this callback is intentionally separate from the attacker modules.
 */
static ssize_t safety_mem_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char kbuf[DEMO_COMMAND_BUFFER_SIZE] = {0};
	char *cmd;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	/* User pointers cannot be dereferenced directly in kernel code;
	 * copy_from_user() validates and copies into a kernel-owned buffer. */
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	cmd = strim(kbuf);

	if (strcmp(cmd, DEMO_ENABLE_COMMAND) == 0 ||
	    strcmp(cmd, DEMO_PROTECT_COMMAND) == 0) {
		safety_mem_set_protection(true);
	} else if (strcmp(cmd, DEMO_DISABLE_COMMAND) == 0 ||
		   strcmp(cmd, DEMO_UNPROTECT_COMMAND) == 0) {
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

/** @brief Allocate and initialize the shared safety page and interfaces. */
static int __init safety_mem_init(void)
{
	struct pgtable_walk_info info;

	/* alloc_pages(order 0) obtains one physical page frame and may sleep because
	 * GFP_KERNEL is used from module initialization context. */
	g_safety_page = alloc_pages(GFP_KERNEL, 0);
	if (!g_safety_page) {
		pr_err("Failed to allocate safety page frame\n");
		return -ENOMEM;
	}

	/* page_address() returns the kernel direct-map virtual alias, while
	 * page_to_phys() exposes the address of the backing physical frame. */
	g_virt_addr = page_address(g_safety_page);
	g_phys_addr = page_to_phys(g_safety_page);
	/* vmap() creates a second virtual alias to the same page. VM_ALLOC makes
	 * that alias eligible for set_memory_ro()/set_memory_rw(). */
	g_vmalloc_addr = vmap(&g_safety_page, 1, VM_MAP | VM_ALLOC, PAGE_KERNEL);

	safety_buf_ptr = (u32 *)g_virt_addr;

	*(u32 *)g_virt_addr = SAFETY_SENTINEL;
	asm volatile("dsb sy\n\tisb\n" ::: "memory");

	ctx_monitor_set_protected_range((unsigned long)g_virt_addr,
					 (unsigned long)g_virt_addr + PAGE_SIZE);

	safety_mem_walk_pgtable((unsigned long)g_virt_addr, &info);
	g_initial_mapping = info;
	pr_info("Initial page table walk: virt=0x%px phys=0x%pa block=%d writable=%d\n",
		g_virt_addr, &g_phys_addr, info.is_block_mapping, info.is_writable);

	/* proc_create() exposes the seq_file status reader and command writer at
	 * /proc/safety_mem_status. */
	if (!proc_create("safety_mem_status", DEMO_PROC_CONTROL_MODE, NULL,
			 &safety_mem_proc_ops)) {
		pr_err("Failed to create /proc/safety_mem_status\n");
		if (g_vmalloc_addr)
			vunmap(g_vmalloc_addr);
		__free_pages(g_safety_page, 0);
		return -ENOMEM;
	}

	pr_info("Module loaded successfully: virt=0x%px phys=0x%pa vmalloc=0x%px\n",
		g_virt_addr, &g_phys_addr, g_vmalloc_addr);
	return 0;
}

/** @brief Release the safety page and remove exported interfaces. */
static void __exit safety_mem_exit(void)
{
	/* Remove userspace access first so no new proc operation can start. */
	remove_proc_entry("safety_mem_status", NULL);

	safety_buf_ptr = NULL;

	if (safety_mem_is_protected()) {
		set_memory_rw((unsigned long)g_virt_addr, 1);
		if (g_vmalloc_addr)
			set_memory_rw((unsigned long)g_vmalloc_addr, 1);
	}

	/* vunmap() destroys only the extra virtual alias, not the physical page. */
	if (g_vmalloc_addr)
		vunmap(g_vmalloc_addr);

	/* Return the physical page frame to the kernel page allocator. */
	if (g_safety_page)
		__free_pages(g_safety_page, 0);

	pr_info("Module unloaded cleanly\n");
}

/* Register the module-loader callbacks used for insmod and rmmod. */
module_init(safety_mem_init);
module_exit(safety_mem_exit);

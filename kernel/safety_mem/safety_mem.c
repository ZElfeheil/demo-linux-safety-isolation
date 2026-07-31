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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("ARM64 Linux 6.6 Safety Memory Isolation Module");
MODULE_VERSION("1.0");

/* Global Memory References & Mutex */
static struct page *g_safety_page;
static void *g_virt_addr;
static void *g_vmalloc_addr;
static phys_addr_t g_phys_addr;

u32 *safety_buf_ptr = NULL;
EXPORT_SYMBOL_GPL(safety_buf_ptr);

extern struct mutex safety_mutex;
extern void ctx_monitor_set_protected_range(unsigned long start, unsigned long end);

static atomic_t g_ctx_protected = ATOMIC_INIT(0);
static atomic_t g_smmu_active = ATOMIC_INIT(0);

/* Exported Accessors */
void *safety_mem_get_virt_addr(void)
{
	return g_virt_addr;
}
EXPORT_SYMBOL_GPL(safety_mem_get_virt_addr);

void *safety_mem_get_vmalloc_addr(void)
{
	return g_vmalloc_addr;
}
EXPORT_SYMBOL_GPL(safety_mem_get_vmalloc_addr);

phys_addr_t safety_mem_get_phys_addr(void)
{
	return g_phys_addr;
}
EXPORT_SYMBOL_GPL(safety_mem_get_phys_addr);

struct mutex *safety_mem_get_mutex(void)
{
	return &safety_mutex;
}
EXPORT_SYMBOL_GPL(safety_mem_get_mutex);

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

static void safety_mem_walk_pgtable(unsigned long addr, struct pgtable_walk_info *info)
{
	memset(info, 0, sizeof(*info));
	info->virt_addr = addr;

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
		info->is_block_mapping = true;
		info->is_valid = true;
		info->is_writable = pmd_write(*pmd);
		info->phys_addr = (phys_addr_t)pmd_pfn(*pmd) << PAGE_SHIFT;
		return;
	}

	pte_t *pte = pte_offset_kernel(pmd, addr);
	if (!pte || pte_none(*pte))
		return;
	info->pte_val = *pte;
	info->is_block_mapping = false;
	info->is_valid = pte_present(*pte);
	info->is_writable = pte_write(*pte);
	info->phys_addr = (phys_addr_t)pte_pfn(*pte) << PAGE_SHIFT;
}

int safety_mem_set_protection(bool enable_ro)
{
	int ret = 0;
	struct pgtable_walk_info info;

	mutex_lock(&safety_mutex);

	if (enable_ro) {
		ctx_monitor_set_protected_range((unsigned long)g_virt_addr,
						 (unsigned long)g_virt_addr + PAGE_SIZE);
		pr_info("Enabling RO protection on 0x%px (vmalloc: 0x%px)\n",
			g_virt_addr, g_vmalloc_addr);
		ret = set_memory_ro((unsigned long)g_virt_addr, 1);
		if (ret)
			pr_err("set_memory_ro virt_addr failed: %d\n", ret);
		if (g_vmalloc_addr) {
			ret = set_memory_ro((unsigned long)g_vmalloc_addr, 1);
			if (ret)
				pr_err("set_memory_ro vmalloc_addr failed: %d\n", ret);
		}
		/* ARM64 memory pipeline synchronization */
		asm volatile("dsb sy\n\tisb\n" ::: "memory");
		atomic_set(&g_ctx_protected, 1);
	} else {
		ctx_monitor_set_protected_range(0, 0);
		pr_info("Disabling RO protection (setting RW) on 0x%px\n", g_virt_addr);
		ret = set_memory_rw((unsigned long)g_virt_addr, 1);
		if (ret)
			pr_err("set_memory_rw virt_addr failed: %d\n", ret);
		if (g_vmalloc_addr) {
			ret = set_memory_rw((unsigned long)g_vmalloc_addr, 1);
			if (ret)
				pr_err("set_memory_rw vmalloc_addr failed: %d\n", ret);
		}
		asm volatile("dsb sy\n\tisb\n" ::: "memory");
		atomic_set(&g_ctx_protected, 0);
	}

	mutex_unlock(&safety_mutex);

	safety_mem_walk_pgtable((unsigned long)g_virt_addr, &info);
	pr_info("PTE walk after protection toggle: valid=%d block=%d writable=%d phys=0x%pa\n",
		info.is_valid, info.is_block_mapping, info.is_writable, &info.phys_addr);

	return ret;
}
EXPORT_SYMBOL_GPL(safety_mem_set_protection);

int safety_mem_safe_write(u32 val)
{
	bool was_ro;

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

static int safety_mem_proc_show(struct seq_file *m, void *v)
{
	u32 val_vmalloc = 0;
	u32 val_phys = 0;
	int protected_state = atomic_read(&g_ctx_protected);
	int smmu_state = atomic_read(&g_smmu_active);
	char owner_buf[TASK_COMM_LEN] = "none";

	if (g_vmalloc_addr) {
		copy_from_kernel_nofault(&val_vmalloc, g_vmalloc_addr, sizeof(val_vmalloc));
	}

	if (g_virt_addr) {
		copy_from_kernel_nofault(&val_phys, g_virt_addr, sizeof(val_phys));
	}

	if (mutex_is_locked(&safety_mutex)) {
		unsigned long owner_val = (unsigned long)atomic_long_read(&safety_mutex.owner);
		struct task_struct *owner = (struct task_struct *)(owner_val & ~0x07UL);
		if (owner)
			get_task_comm(owner_buf, owner);
		else
			strscpy(owner_buf, "unknown", sizeof(owner_buf));
	}

	seq_printf(m, "virt_addr: 0x%px\n", g_virt_addr);
	seq_printf(m, "phys_addr: 0x%pa\n", &g_phys_addr);
	seq_printf(m, "value_via_vmalloc: 0x%08X\n", val_vmalloc);
	seq_printf(m, "value_via_phys: 0x%08X\n", val_phys);
	seq_printf(m, "ctx_protected: %d\n", protected_state);
	seq_printf(m, "smmu_active: %d\n", smmu_state);
	seq_printf(m, "mutex_owner: %s\n", owner_buf);
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
	char *cmd;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	cmd = strim(kbuf);

	if (strcmp(cmd, "1") == 0 || strcmp(cmd, "protect") == 0) {
		safety_mem_set_protection(true);
	} else if (strcmp(cmd, "0") == 0 || strcmp(cmd, "unprotect") == 0) {
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
	struct pgtable_walk_info info;

	g_safety_page = alloc_pages(GFP_KERNEL, 0);
	if (!g_safety_page) {
		pr_err("Failed to allocate safety page frame\n");
		return -ENOMEM;
	}

	g_virt_addr = page_address(g_safety_page);
	g_phys_addr = page_to_phys(g_safety_page);
	g_vmalloc_addr = vmap(&g_safety_page, 1, VM_MAP, PAGE_KERNEL);

	safety_buf_ptr = (u32 *)g_virt_addr;

	*(u32 *)g_virt_addr = SAFETY_SENTINEL;
	asm volatile("dsb sy\n\tisb\n" ::: "memory");

	ctx_monitor_set_protected_range((unsigned long)g_virt_addr,
					 (unsigned long)g_virt_addr + PAGE_SIZE);

	safety_mem_walk_pgtable((unsigned long)g_virt_addr, &info);
	pr_info("Initial page table walk: virt=0x%px phys=0x%pa block=%d writable=%d\n",
		g_virt_addr, &g_phys_addr, info.is_block_mapping, info.is_writable);

	if (!proc_create("safety_mem_status", 0666, NULL, &safety_mem_proc_ops)) {
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

static void __exit safety_mem_exit(void)
{
	remove_proc_entry("safety_mem_status", NULL);

	safety_buf_ptr = NULL;

	if (safety_mem_is_protected()) {
		set_memory_rw((unsigned long)g_virt_addr, 1);
		if (g_vmalloc_addr)
			set_memory_rw((unsigned long)g_vmalloc_addr, 1);
	}

	if (g_vmalloc_addr)
		vunmap(g_vmalloc_addr);

	if (g_safety_page)
		__free_pages(g_safety_page, 0);

	pr_info("Module unloaded cleanly\n");
}

module_init(safety_mem_init);
module_exit(safety_mem_exit);

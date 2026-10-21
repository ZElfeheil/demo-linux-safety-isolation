// SPDX-License-Identifier: GPL-2.0
/*
 * bad_driver.c - Rogue Driver Attack Simulator Kernel Module
 *
 * Simulates 3 attack modes:
 * Mode 1: vmalloc mapping write bypass attempt
 * Mode 2: Unsynchronized write bypassing mutex synchronization
 * Mode 3: Physical linear mapping (phys_to_virt) write bypass attempt
 *
 * Exposes /proc/bad_driver_ts interface for user-space triggers and status logging.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <asm/io.h>

#include "../safety_mem/safety_mem.h"
#include "../demo_constants.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("Rogue Driver Attack Simulator Module");
MODULE_VERSION("1.0");

/* Attack State Tracking Variables */
static u64 g_last_attack_ts_ns;
static int g_last_attack_mode;
static unsigned long g_last_target_addr;
static char g_last_result[DEMO_RESULT_BUFFER_SIZE] = "NONE";
static u32 g_attack_count;

/** @brief Attempt a fault-tolerant write through a protected virtual address. */
static int execute_attack_mode_1(void)
{
	void *vaddr = safety_mem_get_vmalloc_addr();
	u32 val = BAD_DRIVER_VMALLOC_VALUE;
	int ret;

	if (!vaddr)
		vaddr = safety_mem_get_virt_addr();

	g_last_target_addr = (unsigned long)vaddr;
	g_last_attack_mode = BAD_DRIVER_ATTACK_VMALLOC;
	/* ktime_get_real_ns() timestamps the attempt using wall-clock nanoseconds. */
	g_last_attack_ts_ns = ktime_get_real_ns();
	g_attack_count++;

	/* copy_to_kernel_nofault() attempts the write and returns -EFAULT instead
	 * of letting a protected destination crash this demonstration path. */
	ret = copy_to_kernel_nofault(vaddr, &val, sizeof(val));
	if (ret != 0) {
		snprintf(g_last_result, sizeof(g_last_result), "BLOCKED_EFAULT");
		pr_warn("Attack Mode 1 write to 0x%px BLOCKED by memory protection (ret=%d)\n",
			vaddr, ret);
	} else {
		snprintf(g_last_result, sizeof(g_last_result), "SUCCESS_UNPROTECTED");
		pr_info("Attack Mode 1 write to 0x%px SUCCEEDED\n", vaddr);
	}
	return ret;
}

/** @brief Attempt an unsynchronized write that ignores the shared mutex. */
static int execute_attack_mode_2(void)
{
	void *vaddr = safety_mem_get_virt_addr();
	struct mutex *smutex = safety_mem_get_mutex();
	u32 val = BAD_DRIVER_MUTEX_BYPASS_VALUE;
	bool was_locked;
	int ret;

	g_last_target_addr = (unsigned long)vaddr;
	g_last_attack_mode = BAD_DRIVER_ATTACK_MUTEX_BYPASS;
	g_last_attack_ts_ns = ktime_get_real_ns();
	g_attack_count++;

	/* mutex_is_locked() observes the lock but does not acquire it; the following
	 * write intentionally bypasses mutex_lock(). */
	was_locked = smutex ? mutex_is_locked(smutex) : false;
	ret = copy_to_kernel_nofault(vaddr, &val, sizeof(val));
	if (ret != 0) {
		snprintf(g_last_result, sizeof(g_last_result), "BLOCKED_EFAULT");
		pr_warn("Attack Mode 2 unsynchronized write BLOCKED (ret=%d)\n", ret);
	} else {
		snprintf(g_last_result, sizeof(g_last_result),
			 was_locked ? "MUTEX_BYPASS_RACE" : "SUCCESS_UNPROTECTED");
		pr_warn("Attack Mode 2 write performed without mutex (was_locked=%d)\n", was_locked);
	}
	return ret;
}

/** @brief Attempt a physical linear-map alias write using the page address. */
static int execute_attack_mode_3(void)
{
	phys_addr_t pa = safety_mem_get_phys_addr();
	/* phys_to_virt() converts RAM's physical address into the kernel linear-map
	 * alias, demonstrating that protecting another alias may be insufficient. */
	void *va_alias = phys_to_virt(pa);
	u32 val = BAD_DRIVER_LINEAR_ALIAS_VALUE;
	int ret;

	g_last_target_addr = (unsigned long)va_alias;
	g_last_attack_mode = BAD_DRIVER_ATTACK_LINEAR_ALIAS;
	g_last_attack_ts_ns = ktime_get_real_ns();
	g_attack_count++;

	/* Attack Mode 3: Physical linear mapping bypass attempt via phys_to_virt / __va */
	ret = copy_to_kernel_nofault(va_alias, &val, sizeof(val));
	if (ret != 0) {
		snprintf(g_last_result, sizeof(g_last_result), "BLOCKED_EFAULT");
		pr_warn("Attack Mode 3 linear map bypass (pa=0x%pa, va=0x%px) BLOCKED (ret=%d)\n",
			&pa, va_alias, ret);
	} else {
		snprintf(g_last_result, sizeof(g_last_result), "SUCCESS_BYPASS");
		pr_info("Attack Mode 3 linear map write to 0x%px SUCCEEDED\n", va_alias);
	}
	return ret;
}

/** @brief Render the most recent attack mode and target address. */
static int bad_driver_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "last_attack_timestamp_ns: %llu\n", g_last_attack_ts_ns);
	seq_printf(m, "last_attack_mode: %d\n", g_last_attack_mode);
	seq_printf(m, "target_addr: 0x%lx\n", g_last_target_addr);
	seq_printf(m, "result: %s\n", g_last_result);
	seq_printf(m, "attack_count: %u\n", g_attack_count);
	return 0;
}

/** @brief Open the attack simulator's procfs control endpoint. */
static int bad_driver_proc_open(struct inode *inode, struct file *file)
{
	/* single_open() connects one seq_file reader to bad_driver_proc_show(). */
	return single_open(file, bad_driver_proc_show, NULL);
}

/**
 * @brief Parse and execute an attack command received through procfs.
 * @param file Open procfs file state.
 * @param buf User-space command buffer.
 * @param count Number of bytes supplied by user space.
 * @param ppos Procfs file position.
 * @return Number of bytes consumed, or a negative errno on failure.
 */
static ssize_t bad_driver_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char kbuf[DEMO_COMMAND_BUFFER_SIZE] = {0};
	char *cmd;
	unsigned int mode;

	if (count >= sizeof(kbuf))
		return -EINVAL;
	/* Safely copy the command out of userspace before parsing it in kernel memory. */
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	cmd = strim(kbuf);
	/* kstrtouint() performs checked text-to-integer conversion in the kernel. */
	if (kstrtouint(cmd, 0, &mode))
		return -EINVAL;

	switch (mode) {
	case BAD_DRIVER_ATTACK_VMALLOC:
		execute_attack_mode_1();
		break;
	case BAD_DRIVER_ATTACK_MUTEX_BYPASS:
		execute_attack_mode_2();
		break;
	case BAD_DRIVER_ATTACK_LINEAR_ALIAS:
		execute_attack_mode_3();
		break;
	default:
		return -EINVAL;
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

/** @brief Initialize the attack simulator and procfs control endpoint. */
static int __init bad_driver_init(void)
{
	/* proc_create() publishes both status reads and attack-command writes. */
	if (!proc_create("bad_driver_ts", DEMO_PROC_CONTROL_MODE, NULL, &bad_driver_proc_ops)) {
		pr_err("Failed to create /proc/bad_driver_ts\n");
		return -ENOMEM;
	}
	pr_info("Loaded successfully\n");
	return 0;
}

/** @brief Remove the attack simulator control endpoint. */
static void __exit bad_driver_exit(void)
{
	remove_proc_entry("bad_driver_ts", NULL);
	pr_info("Unloaded cleanly\n");
}

/* Register the module-loader callbacks used by insmod and rmmod. */
module_init(bad_driver_init);
module_exit(bad_driver_exit);

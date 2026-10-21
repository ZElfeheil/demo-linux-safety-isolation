// SPDX-License-Identifier: GPL-2.0
/*
 * ctx_monitor.c - ARM64 Exception & Fault Monitoring Kernel Module
 *
 * Traps DIE_PAGE_FAULT kernel exceptions via die_notifier at INT_MAX priority.
 * Logs faulting PC, fault address, and process context to /proc/ctx_monitor_log.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <asm/ptrace.h>

#include "../demo_constants.h"

#define MODULE_NAME "ctx_monitor"
#define PROC_FILENAME "ctx_monitor_log"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("ARM64 Exception & Fault Monitoring Kernel Module");
MODULE_VERSION("1.0");

struct fault_log_entry {
	u64 timestamp_ns;
	unsigned long pc;
	unsigned long fault_addr;
	pid_t pid;
	char comm[TASK_COMM_LEN];
	long err_code;
	int trap_nr;
};

static struct fault_log_entry log_ring[DEMO_LOG_CAPACITY];
static size_t log_head;
static size_t log_count;
static DEFINE_SPINLOCK(log_lock);

static unsigned long protected_range_start;
static unsigned long protected_range_end;

/**
 * @brief Configure the address range reported as safety-critical.
 * @param start Inclusive start virtual address of the protected range.
 * @param end Exclusive end virtual address of the protected range.
 */
void ctx_monitor_set_protected_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	/* spin_lock_irqsave() protects state shared with exception context and also
	 * prevents a local interrupt from re-entering the same critical section. */
	spin_lock_irqsave(&log_lock, flags);
	protected_range_start = start;
	protected_range_end = end;
	spin_unlock_irqrestore(&log_lock, flags);

	pr_info("Protected range set to [0x%lx - 0x%lx]\n", start, end);
}
EXPORT_SYMBOL_GPL(ctx_monitor_set_protected_range);

/** @brief Append a fault observation to the monitor's bounded ring buffer. */
static void log_fault_event(struct pt_regs *regs, unsigned long fault_addr,
			    long err, int trapnr)
{
	unsigned long flags;
	struct fault_log_entry *entry;

	/* Fault callbacks cannot sleep, so use the IRQ-safe spinlock rather than a mutex. */
	spin_lock_irqsave(&log_lock, flags);

	entry = &log_ring[log_head];
	/* The fast time accessor is suitable for this non-sleeping diagnostic path. */
	entry->timestamp_ns = ktime_get_real_fast_ns();
	entry->pc = regs ? regs->pc : 0;
	entry->fault_addr = fault_addr;
	/* current identifies the task executing when the exception was reported. */
	entry->pid = current->pid;
	get_task_comm(entry->comm, current);
	entry->err_code = err;
	entry->trap_nr = trapnr;

	log_head = (log_head + 1) % DEMO_LOG_CAPACITY;
	if (log_count < DEMO_LOG_CAPACITY)
		log_count++;

	spin_unlock_irqrestore(&log_lock, flags);

	pr_warn("TRAPPED FAULT pc=0x%llx addr=0x%lx pid=%d comm=%s\n",
		(unsigned long long)(regs ? regs->pc : 0), fault_addr, current->pid, current->comm);
}

/**
 * @brief Observe kernel exceptions and log faults in the protected range.
 * @param nb Registered notifier block.
 * @param val Exception notification value.
 * @param data Architecture-specific die notification payload.
 * @return NOTIFY_DONE after inspecting the notification.
 */
static int ctx_die_notifier_cb(struct notifier_block *nb, unsigned long val,
			       void *data)
{
	struct die_args *args = (struct die_args *)data;
	struct pt_regs *regs;
	unsigned long fault_addr;

	if (!args || !args->regs)
		return NOTIFY_OK;

	regs = args->regs;
	fault_addr = (unsigned long)instruction_pointer(regs);

	if (protected_range_start != 0 && protected_range_end != 0) {
		if (fault_addr < protected_range_start || fault_addr >= protected_range_end)
			return NOTIFY_OK;
	}

	log_fault_event(regs, fault_addr, args->err, args->trapnr);

	return NOTIFY_OK;
}

static struct notifier_block ctx_die_nb = {
	.notifier_call = ctx_die_notifier_cb,
	.priority = INT_MAX,
};

/** @brief Render the bounded fault history through procfs. */
static int ctx_monitor_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	size_t count, i;
	struct fault_log_entry *snapshot;

	/* Copy the ring under the spinlock, then format it after unlocking; seq_printf()
	 * may do work that should not occur inside an IRQ-disabled critical section. */
	snapshot = kmalloc_array(DEMO_LOG_CAPACITY, sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	spin_lock_irqsave(&log_lock, flags);
	count = log_count;
	for (i = 0; i < count; i++) {
		size_t idx;
		if (log_count == DEMO_LOG_CAPACITY)
			idx = (log_head + i) % DEMO_LOG_CAPACITY;
		else
			idx = i;
		snapshot[i] = log_ring[idx];
	}
	spin_unlock_irqrestore(&log_lock, flags);

	for (i = 0; i < count; i++) {
		seq_printf(m, "[%llu.000] FAULT pc=0x%016lx addr=0x%016lx pid=%d comm=%s err=0x%lx trap=%d\n",
			   snapshot[i].timestamp_ns, snapshot[i].pc, snapshot[i].fault_addr,
			   snapshot[i].pid, snapshot[i].comm, snapshot[i].err_code, snapshot[i].trap_nr);
	}

	kfree(snapshot);
	return 0;
}

/** @brief Open the context-monitor procfs status endpoint. */
static int ctx_monitor_proc_open(struct inode *inode, struct file *file)
{
	/* single_open() binds this proc file to one complete seq_file snapshot. */
	return single_open(file, ctx_monitor_proc_show, NULL);
}

static const struct proc_ops ctx_monitor_proc_ops = {
	.proc_open    = ctx_monitor_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/** @brief Register the exception notifier and procfs reporting endpoint. */
static int __init ctx_monitor_init(void)
{
	int ret;

	/* register_die_notifier() subscribes to kernel exception notifications;
	 * INT_MAX priority makes this observer run before lower-priority notifiers. */
	ret = register_die_notifier(&ctx_die_nb);
	if (ret) {
		pr_err("Failed to register die notifier (err=%d)\n", ret);
		return ret;
	}

	/* Expose the captured fault ring as /proc/ctx_monitor_log. */
	if (!proc_create(PROC_FILENAME, DEMO_PROC_READ_ONLY_MODE, NULL,
			 &ctx_monitor_proc_ops)) {
		pr_err("Failed to create /proc/%s\n", PROC_FILENAME);
		unregister_die_notifier(&ctx_die_nb);
		return -ENOMEM;
	}

	pr_info("Module loaded. Registered die_notifier at INT_MAX priority.\n");
	return 0;
}

/** @brief Unregister monitoring hooks and release procfs state. */
static void __exit ctx_monitor_exit(void)
{
	/* Stop new userspace reads and unregister the exception callback. */
	remove_proc_entry(PROC_FILENAME, NULL);
	unregister_die_notifier(&ctx_die_nb);
	pr_info("Module unloaded cleanly.\n");
}

/* Register the module-loader callbacks used by insmod and rmmod. */
module_init(ctx_monitor_init);
module_exit(ctx_monitor_exit);

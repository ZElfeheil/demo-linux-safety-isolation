// SPDX-License-Identifier: GPL-2.0
/*
 * rogue_thread.c - Thread C (Rogue / Contractor Thread) Implementation
 *
 * Simulates an uncooperative or rogue kernel thread that performs unsynchronized memory
 * writes to shared safety memory without acquiring safety_mutex, or tampers with lock metadata.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/ftrace.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "mutex_threads.h"
#include "../demo_constants.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("Rogue Kernel Thread simulating uncooperative memory access");
MODULE_VERSION("1.0");

static unsigned int attack_mode;
module_param(attack_mode, uint, DEMO_MODULE_PARAM_MODE);
MODULE_PARM_DESC(attack_mode, "0: Unsynchronized write, 1: Lock metadata attack");

static unsigned int corrupt_val = CORRUPT_SENTINEL;
module_param(corrupt_val, uint, DEMO_MODULE_PARAM_MODE);
MODULE_PARM_DESC(corrupt_val, "Value written by rogue thread (default: 0xDEADDEAD)");

static unsigned int interval_ms = ROGUE_WRITE_INTERVAL_MS;
module_param(interval_ms, uint, DEMO_MODULE_PARAM_MODE);
MODULE_PARM_DESC(interval_ms, "Attack write frequency in ms (default: 300)");

extern u32 *safety_buf_ptr;

static struct task_struct *task_c;
static struct proc_dir_entry *status_proc_entry;
static atomic_t unsynchronized_write_counter = ATOMIC_INIT(0);
static atomic_t metadata_attack_counter = ATOMIC_INIT(0);

/**
 * @brief Emit the rogue thread's execution evidence through procfs.
 * @param seq Destination sequence file supplied by procfs.
 * @param unused Unused iterator context.
 * @return Always zero after writing the current mode and attack counters.
 *
 * Output fields:
 * - attack_mode: selected rogue attack mode.
 * - unsynchronized_write_count: completed data writes without the mutex.
 * - metadata_attack_count: completed writes to safety_mutex.owner.
 */
static int rogue_status_show(struct seq_file *seq, void *unused)
{
	seq_printf(seq, "attack_mode: %u\n", attack_mode);
	seq_printf(seq, "unsynchronized_write_count: %d\n",
		   atomic_read(&unsynchronized_write_counter));
	seq_printf(seq, "metadata_attack_count: %d\n",
		   atomic_read(&metadata_attack_counter));
	return 0;
}

/** @brief Execute the selected uncooperative memory-corruption attack. */
static int rogue_thread_c_fn(void *data)
{
	pr_info("Thread C (Rogue) started in attack_mode=%u\n", attack_mode);
	/* Send the live demonstration event to ftrace/trace_pipe for the monitor. */
	trace_printk("rogue_thread: Rogue Thread C started (attack_mode=%u)\n", attack_mode);

	while (!kthread_should_stop()) {
		if (attack_mode == ROGUE_ATTACK_UNSYNCHRONIZED_WRITE) {
			/* Unsynchronized write bypassing mutex */
			if (safety_buf_ptr) {
				/* WRITE_ONCE() guarantees one visible store, but deliberately
				 * provides no locking or authorization by itself. */
				WRITE_ONCE(*safety_buf_ptr, corrupt_val);
				atomic_inc(&unsynchronized_write_counter);
				trace_printk("rogue_thread: Unsynchronized write 0x%08x to safety_buf WITHOUT LOCK\n",
					     corrupt_val);
			}
		} else if (attack_mode == ROGUE_ATTACK_MUTEX_METADATA) {
			/* Lock Metadata Attack - corrupt mutex struct in RAM */
			if (mutex_is_locked(&safety_mutex)) {
				/* atomic_long_set() overwrites the kernel mutex owner field as
				 * one machine-visible operation; this is intentional corruption. */
				atomic_long_set(&safety_mutex.owner, 0);
				atomic_inc(&metadata_attack_counter);
				trace_printk("rogue_thread: METADATA ATTACK: Cleared mutex.owner while locked!\n");
			}
		}

		/* Yield the CPU between attacks and remain stoppable during teardown. */
		msleep_interruptible(interval_ms);
	}

	pr_info("Thread C stopping\n");
	return 0;
}

/** @brief Start the rogue attacker thread. */
static int __init rogue_thread_init(void)
{
	pr_info("Initializing rogue_thread module\n");
	atomic_set(&unsynchronized_write_counter, 0);
	atomic_set(&metadata_attack_counter, 0);

	/* proc_create_single() creates a read-only procfs file backed by the
	 * rogue_status_show() seq_file callback. */
	status_proc_entry = proc_create_single(DEMO_ROGUE_STATUS_PROC_NAME,
					       DEMO_PROC_READ_ONLY_MODE, NULL,
					       rogue_status_show);
	if (!status_proc_entry) {
		pr_err("Failed to create /proc/%s\n", DEMO_ROGUE_STATUS_PROC_NAME);
		return -ENOMEM;
	}

	/* Create and immediately schedule the independent attacker task. */
	task_c = kthread_run(rogue_thread_c_fn, NULL, "rogue_thread");
	if (IS_ERR(task_c)) {
		pr_err("Failed to create Thread C\n");
		proc_remove(status_proc_entry);
		status_proc_entry = NULL;
		return PTR_ERR(task_c);
	}

	return 0;
}

/** @brief Stop the rogue attacker thread. */
static void __exit rogue_thread_exit(void)
{
	pr_info("Cleaning up rogue_thread module\n");
	/* Stop the task before removing the proc entry that reports its counters. */
	if (task_c)
		kthread_stop(task_c);
	proc_remove(status_proc_entry);
	status_proc_entry = NULL;
}

/* Register the entry and exit hooks used by insmod and rmmod. */
module_init(rogue_thread_init);
module_exit(rogue_thread_exit);

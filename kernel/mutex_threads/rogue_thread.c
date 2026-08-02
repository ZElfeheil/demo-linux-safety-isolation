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

#include "mutex_threads.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("Rogue Kernel Thread simulating uncooperative memory access");
MODULE_VERSION("1.0");

static unsigned int attack_mode;
module_param(attack_mode, uint, 0644);
MODULE_PARM_DESC(attack_mode, "0: Unsynchronized write, 1: Lock metadata attack");

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
			/* Unsynchronized write bypassing mutex */
			if (safety_buf_ptr) {
				WRITE_ONCE(*safety_buf_ptr, corrupt_val);
				trace_printk("rogue_thread: Unsynchronized write 0x%08x to safety_buf WITHOUT LOCK\n",
					     corrupt_val);
			}
		} else if (attack_mode == 1) {
			/* Lock Metadata Attack - corrupt mutex struct in RAM */
			if (mutex_is_locked(&safety_mutex)) {
				atomic_long_set(&safety_mutex.owner, 0);
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

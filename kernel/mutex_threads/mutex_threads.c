// SPDX-License-Identifier: GPL-2.0
/*
 * mutex_threads.c - Safety Thread A and Cooperative Thread B Implementation
 *
 * Demonstrates thread synchronization using kernel mutexes. Thread A holds the mutex
 * across a window to detect uncooperative concurrent writes. Thread B respects
 * the mutex lock and restores the expected sentinel value.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>
#include <linux/sched.h>

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

/* External Mutex (from safety_mem.ko) */
extern struct mutex safety_mutex;

/* External Safety Memory Pointer (from safety_mem.ko) */
extern u32 *safety_buf_ptr;

/* Task & Counter Tracking */
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
		unsigned long owner_val = (unsigned long)atomic_long_read(&safety_mutex.owner);
		struct task_struct *owner = (struct task_struct *)(owner_val & ~0x07UL);
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

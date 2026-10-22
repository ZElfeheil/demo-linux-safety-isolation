/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MUTEX_THREADS_H
#define _MUTEX_THREADS_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include "../demo_constants.h"

/** Snapshot of cooperative thread and mutex state for the userspace monitor. */
struct mutex_status_info {
	bool is_locked;
	pid_t owner_pid;
	char owner_name[TASK_COMM_LEN];
	u32 contention_count;
	u32 violation_count;
};

/** Shared mutex used by the cooperative safety threads. */
extern struct mutex safety_mutex;
/**
 * Populate a caller-provided snapshot of mutex and thread activity.
 * @param info Output structure filled with the current owner and counters.
 * The caller must provide valid storage for the complete structure.
 */
void get_safety_mutex_status(struct mutex_status_info *info);

#endif /* _MUTEX_THREADS_H */

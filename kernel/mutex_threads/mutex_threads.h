/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MUTEX_THREADS_H
#define _MUTEX_THREADS_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#ifndef SAFETY_SENTINEL
#define SAFETY_SENTINEL   0x5AFE1234U
#endif

#ifndef CORRUPT_SENTINEL
#define CORRUPT_SENTINEL  0xDEADDEADU
#endif

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

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SAFETY_MEM_H
#define _SAFETY_MEM_H

#include <linux/types.h>
#include <linux/mutex.h>

#define SAFETY_SENTINEL   0x5AFE1234U
#define CORRUPT_SENTINEL  0xDEADDEADU

/* Exported Function Protocols */
void *safety_mem_get_virt_addr(void);
void *safety_mem_get_vmalloc_addr(void);
phys_addr_t safety_mem_get_phys_addr(void);
struct mutex *safety_mem_get_mutex(void);
int safety_mem_set_protection(bool enable_ro);
int safety_mem_safe_write(u32 val);
bool safety_mem_is_protected(void);

/* Exported Global Buffer Pointer */
extern u32 *safety_buf_ptr;

#endif /* _SAFETY_MEM_H */

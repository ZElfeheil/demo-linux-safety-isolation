/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SAFETY_MEM_H
#define _SAFETY_MEM_H

#include <linux/types.h>
#include <linux/mutex.h>
#include "../demo_constants.h"

/** Return the direct kernel virtual address of the safety page. */
void *safety_mem_get_virt_addr(void);
/** Return the vmalloc alias used to demonstrate PTE protection. */
void *safety_mem_get_vmalloc_addr(void);
/** Return the physical address backing the safety page. */
phys_addr_t safety_mem_get_phys_addr(void);
/** Return the mutex associated with the safety buffer. */
struct mutex *safety_mem_get_mutex(void);
/**
 * Enable or disable read-only protection for the safety mappings.
 * @param enable_ro true to protect the mappings, false to restore write access.
 * @return 0 on success, or a negative errno when the page-table update fails.
 */
int safety_mem_set_protection(bool enable_ro);
/**
 * Perform an owner-controlled write to the safety buffer.
 * @param val Value written to the shared safety buffer.
 * @return 0 on success, or a negative errno if the protected write is rejected.
 */
int safety_mem_safe_write(u32 val);
/** Report whether CPU-side protection is currently enabled. */
bool safety_mem_is_protected(void);
/** Update the SMMU guard availability displayed by safety_mem status. */
void safety_mem_set_smmu_active(bool active);

/** Shared pointer to the safety buffer used by the demonstration modules. */
extern u32 *safety_buf_ptr;

#endif /* _SAFETY_MEM_H */

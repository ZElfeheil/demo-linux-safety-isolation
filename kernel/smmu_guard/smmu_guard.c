// SPDX-License-Identifier: GPL-2.0
/*
 * smmu_guard.c - ARM64 SMMUv3 IOMMU Protection & DMA Fault Logger Module
 *
 * Configures IOMMU domain DMA protection hooks for safety memory region.
 * Intercepts and logs unauthorized DMA access attempts to /proc/smmu_guard_log.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/iommu.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/slab.h>

#include "../safety_mem/safety_mem.h"
#include "../demo_constants.h"

#define MODULE_NAME "smmu_guard"
#define PROC_FILENAME "smmu_guard_log"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("ARM64 SMMUv3 IOMMU Protection & DMA Fault Logger Module");
MODULE_VERSION("1.0");
MODULE_SOFTDEP("pre: safety_mem");

struct smmu_fault_entry {
	u64 timestamp_ns;
	phys_addr_t phys_addr;
	size_t size;
	u32 stream_id;
	char dev_name[DEMO_DEVICE_NAME_SIZE];
	char action[DEMO_ACTION_NAME_SIZE];
};

static struct smmu_fault_entry smmu_log_ring[DEMO_LOG_CAPACITY];
static size_t smmu_log_head;
static size_t smmu_log_count;
static DEFINE_SPINLOCK(smmu_log_lock);

static struct iommu_domain *guard_domain;
static struct platform_device *dummy_pdev;

/**
 * @brief Record a DMA transaction rejected by the SMMU guard.
 * @param phys Physical address targeted by the rejected transaction.
 * @param size Number of bytes requested by the device.
 * @param stream_id SMMU stream identifier associated with the request.
 * @param dev_name Device name used in the diagnostic record.
 */
void smmu_guard_log_blocked_dma(phys_addr_t phys, size_t size, u32 stream_id, const char *dev_name)
{
	unsigned long flags;
	struct smmu_fault_entry *entry;

	/* DMA fault reporting may run in atomic context, so protect the ring with an
	 * IRQ-safe spinlock rather than a sleeping mutex. */
	spin_lock_irqsave(&smmu_log_lock, flags);

	entry = &smmu_log_ring[smmu_log_head];
	entry->timestamp_ns = ktime_get_real_fast_ns();
	entry->phys_addr = phys;
	entry->size = size;
	entry->stream_id = stream_id;
	strscpy(entry->dev_name, dev_name ? dev_name : "dummy_dma_dev", sizeof(entry->dev_name));
	strscpy(entry->action, "BLOCKED", sizeof(entry->action));

	smmu_log_head = (smmu_log_head + 1) % DEMO_LOG_CAPACITY;
	if (smmu_log_count < DEMO_LOG_CAPACITY)
		smmu_log_count++;

	spin_unlock_irqrestore(&smmu_log_lock, flags);

	pr_warn("SMMU DMA BLOCKED dev=%s stream_id=0x%x phys=0x%pa size=%zu\n",
		dev_name ? dev_name : "dummy_dma_dev", stream_id, &phys, size);
}
EXPORT_SYMBOL_GPL(smmu_guard_log_blocked_dma);

/** @brief Convert an IOMMU fault callback into a readable log entry. */
static int smmu_iommu_fault_handler(struct iommu_domain *domain, struct device *dev,
				    unsigned long iova, int flags, void *token)
{
	smmu_guard_log_blocked_dma((phys_addr_t)iova, PAGE_SIZE,
				   SMMU_DEMO_STREAM_ID, dev_name(dev));
	return 0;
}

/** @brief Render the bounded SMMU fault history through procfs. */
static int smmu_guard_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	size_t count, i;
	struct smmu_fault_entry *snapshot;

	/* Snapshot under the lock and format afterward so procfs output does not
	 * lengthen the atomic critical section. */
	snapshot = kmalloc_array(DEMO_LOG_CAPACITY, sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	spin_lock_irqsave(&smmu_log_lock, flags);
	count = smmu_log_count;
	for (i = 0; i < count; i++) {
		size_t idx;
		if (smmu_log_count == DEMO_LOG_CAPACITY)
			idx = (smmu_log_head + i) % DEMO_LOG_CAPACITY;
		else
			idx = i;
		snapshot[i] = smmu_log_ring[idx];
	}
	spin_unlock_irqrestore(&smmu_log_lock, flags);

	for (i = 0; i < count; i++) {
		seq_printf(m, "[%llu.000] SMMU_FAULT dev=%s stream_id=0x%x phys=0x%pa size=%zu action=%s\n",
			   snapshot[i].timestamp_ns, snapshot[i].dev_name, snapshot[i].stream_id,
			   &snapshot[i].phys_addr, snapshot[i].size, snapshot[i].action);
	}

	kfree(snapshot);
	return 0;
}

/** @brief Open the SMMU guard procfs status endpoint. */
static int smmu_guard_proc_open(struct inode *inode, struct file *file)
{
	/* single_open() binds this proc read to one seq_file snapshot callback. */
	return single_open(file, smmu_guard_proc_show, NULL);
}

static const struct proc_ops smmu_guard_proc_ops = {
	.proc_open    = smmu_guard_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/** @brief Create the guard domain and register its fault reporting path. */
static int __init smmu_guard_init(void)
{
	int ret;

	/* Register a synthetic device so the demo has an attachable IOMMU endpoint
	 * even though QEMU does not provide a real DMA-producing peripheral here. */
	dummy_pdev = platform_device_register_simple("smmu_dummy_dev", -1, NULL, 0);
	if (IS_ERR(dummy_pdev)) {
		pr_warn("Failed to register dummy platform device, software fallback active\n");
		dummy_pdev = NULL;
	}

	/* Allocate an isolated translation domain and register the callback used
	 * when the IOMMU reports an unauthorized transaction. */
	guard_domain = iommu_domain_alloc(&platform_bus_type);
	if (guard_domain) {
		iommu_set_fault_handler(guard_domain, (iommu_fault_handler_t)smmu_iommu_fault_handler, NULL);
		if (dummy_pdev) {
			/* Attach the synthetic device so its DMA translations use this domain. */
			ret = iommu_attach_device(guard_domain, &dummy_pdev->dev);
			if (ret)
				pr_warn("Could not attach dummy device to IOMMU domain (err=%d)\n", ret);
		}
		pr_info("Hardware SMMUv3 IOMMU domain allocated successfully.\n");
	} else {
		pr_info("SMMUv3 hardware domain not present; operating in software DMA guard mode.\n");
	}

	/* Publish the bounded DMA-fault history through /proc/smmu_guard_log. */
	if (!proc_create(PROC_FILENAME, DEMO_PROC_READ_ONLY_MODE, NULL,
			 &smmu_guard_proc_ops)) {
		pr_err("Failed to create /proc/%s\n", PROC_FILENAME);
		if (guard_domain) {
			if (dummy_pdev)
				iommu_detach_device(guard_domain, &dummy_pdev->dev);
			iommu_domain_free(guard_domain);
		}
		if (dummy_pdev)
			platform_device_unregister(dummy_pdev);
		return -ENOMEM;
	}

	pr_info("Module loaded. /proc/%s ready.\n", PROC_FILENAME);
	safety_mem_set_smmu_active(true);
	return 0;
}

/** @brief Tear down the guard domain and its reporting endpoint. */
static void __exit smmu_guard_exit(void)
{
	safety_mem_set_smmu_active(false);
	remove_proc_entry(PROC_FILENAME, NULL);
	if (guard_domain) {
		/* Detach devices before freeing their translation domain. */
		if (dummy_pdev)
			iommu_detach_device(guard_domain, &dummy_pdev->dev);
		iommu_domain_free(guard_domain);
	}
	if (dummy_pdev)
		platform_device_unregister(dummy_pdev);
	pr_info("Module unloaded cleanly.\n");
}

/* Register the module-loader callbacks used by insmod and rmmod. */
module_init(smmu_guard_init);
module_exit(smmu_guard_exit);

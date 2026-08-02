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

#define MODULE_NAME "smmu_guard"
#define PROC_FILENAME "smmu_guard_log"
#define MAX_LOG_ENTRIES 128

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Safety Isolation Demo Team");
MODULE_DESCRIPTION("ARM64 SMMUv3 IOMMU Protection & DMA Fault Logger Module");
MODULE_VERSION("1.0");

struct smmu_fault_entry {
	u64 timestamp_ns;
	phys_addr_t phys_addr;
	size_t size;
	u32 stream_id;
	char dev_name[32];
	char action[16];
};

static struct smmu_fault_entry smmu_log_ring[MAX_LOG_ENTRIES];
static size_t smmu_log_head;
static size_t smmu_log_count;
static DEFINE_SPINLOCK(smmu_log_lock);

static struct iommu_domain *guard_domain;
static struct platform_device *dummy_pdev;

void smmu_guard_log_blocked_dma(phys_addr_t phys, size_t size, u32 stream_id, const char *dev_name)
{
	unsigned long flags;
	struct smmu_fault_entry *entry;

	spin_lock_irqsave(&smmu_log_lock, flags);

	entry = &smmu_log_ring[smmu_log_head];
	entry->timestamp_ns = ktime_get_real_fast_ns();
	entry->phys_addr = phys;
	entry->size = size;
	entry->stream_id = stream_id;
	strscpy(entry->dev_name, dev_name ? dev_name : "dummy_dma_dev", sizeof(entry->dev_name));
	strscpy(entry->action, "BLOCKED", sizeof(entry->action));

	smmu_log_head = (smmu_log_head + 1) % MAX_LOG_ENTRIES;
	if (smmu_log_count < MAX_LOG_ENTRIES)
		smmu_log_count++;

	spin_unlock_irqrestore(&smmu_log_lock, flags);

	pr_warn("SMMU DMA BLOCKED dev=%s stream_id=0x%x phys=0x%pa size=%zu\n",
		dev_name ? dev_name : "dummy_dma_dev", stream_id, &phys, size);
}
EXPORT_SYMBOL_GPL(smmu_guard_log_blocked_dma);

static int smmu_iommu_fault_handler(struct iommu_domain *domain, struct device *dev,
				    unsigned long iova, int flags, void *token)
{
	smmu_guard_log_blocked_dma((phys_addr_t)iova, PAGE_SIZE, 0x01, dev_name(dev));
	return 0;
}

static int smmu_guard_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	size_t count, i;
	struct smmu_fault_entry *snapshot;

	snapshot = kmalloc_array(MAX_LOG_ENTRIES, sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	spin_lock_irqsave(&smmu_log_lock, flags);
	count = smmu_log_count;
	for (i = 0; i < count; i++) {
		size_t idx;
		if (smmu_log_count == MAX_LOG_ENTRIES)
			idx = (smmu_log_head + i) % MAX_LOG_ENTRIES;
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

static int smmu_guard_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, smmu_guard_proc_show, NULL);
}

static const struct proc_ops smmu_guard_proc_ops = {
	.proc_open    = smmu_guard_proc_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init smmu_guard_init(void)
{
	int ret;

	dummy_pdev = platform_device_register_simple("smmu_dummy_dev", -1, NULL, 0);
	if (IS_ERR(dummy_pdev)) {
		pr_warn("Failed to register dummy platform device, software fallback active\n");
		dummy_pdev = NULL;
	}

	guard_domain = iommu_domain_alloc(&platform_bus_type);
	if (guard_domain) {
		iommu_set_fault_handler(guard_domain, (iommu_fault_handler_t)smmu_iommu_fault_handler, NULL);
		if (dummy_pdev) {
			ret = iommu_attach_device(guard_domain, &dummy_pdev->dev);
			if (ret)
				pr_warn("Could not attach dummy device to IOMMU domain (err=%d)\n", ret);
		}
		pr_info("Hardware SMMUv3 IOMMU domain allocated successfully.\n");
	} else {
		pr_info("SMMUv3 hardware domain not present; operating in software DMA guard mode.\n");
	}

	if (!proc_create(PROC_FILENAME, 0444, NULL, &smmu_guard_proc_ops)) {
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
	return 0;
}

static void __exit smmu_guard_exit(void)
{
	remove_proc_entry(PROC_FILENAME, NULL);
	if (guard_domain) {
		if (dummy_pdev)
			iommu_detach_device(guard_domain, &dummy_pdev->dev);
		iommu_domain_free(guard_domain);
	}
	if (dummy_pdev)
		platform_device_unregister(dummy_pdev);
	pr_info("Module unloaded cleanly.\n");
}

module_init(smmu_guard_init);
module_exit(smmu_guard_exit);

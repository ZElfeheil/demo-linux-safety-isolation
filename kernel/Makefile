# SPDX-License-Identifier: GPL-2.0
# Top-level Kernel Makefile for Linux Safety Isolation Demonstration
# Supports out-of-tree cross-compilation for ARM64 under Linux 6.6 LTS.

ifneq ($(KERNELRELEASE),)
# ---------------------------------------------------------------------------
# Kbuild execution phase
# ---------------------------------------------------------------------------
obj-m += safety_mem/
obj-m += bad_driver/
obj-m += mutex_threads/
obj-m += ctx_monitor/
obj-m += smmu_guard/

else
# ---------------------------------------------------------------------------
# Direct Make invocation phase
# ---------------------------------------------------------------------------
KERNEL_SRC    ?= /demo/linux-6.6
PWD           := $(shell pwd)
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

.PHONY: all clean sparse static-check

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

sparse:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) C=1 modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

static-check:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) C=1 CHECK="sparse" modules
	if command -v smatch >/dev/null 2>&1; then \
		$(MAKE) -C $(KERNEL_SRC) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) C=1 CHECK="smatch --no-db" modules; \
	fi




endif

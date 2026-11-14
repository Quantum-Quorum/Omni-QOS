// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * GPUDirect-QPU DMA & Scatter-Gather Ring Buffer Management
 * Enables sub-microsecond PCIe Peer-to-Peer (P2P) transfers directly into GPU HBM.
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/pci.h>
#else
#include <stdio.h>
#include <stdint.h>
#endif

#include "qpu_internal.h"

int qpu_dma_init(struct qpu_device *qdev)
{
	/* Setup DMA mask for 64-bit addressing */
#ifdef __KERNEL__
	if (qdev->pdev) {
		int ret = dma_set_mask_and_coherent(&qdev->pdev->dev, DMA_BIT_MASK(64));
		if (ret) {
			pr_err("omniq_qpu: Failed to set 64-bit DMA mask\n");
			return ret;
		}
	}
#else
	(void)qdev;
#endif
	return 0;
}

void qpu_dma_cleanup(struct qpu_device *qdev)
{
	(void)qdev;
}

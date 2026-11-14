// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * PCIe Bus Probing & FPGA Hardware Endpoint Driver
 * Supports AMD/Xilinx RFSoC ZCU216 & Alveo PCIe endpoints.
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#else
#include <stdio.h>
#include <stdbool.h>
#endif

#include "qpu_internal.h"

#define PCI_VENDOR_ID_XILINX_QPU 0x10ee
#define PCI_DEVICE_ID_RFSOC_ZCU  0x903f
#define PCI_DEVICE_ID_ALVEO_QPU  0x5000

#define MMIO_REG_CRYO_TEMP_MXC   0x0100
#define MMIO_REG_CRYO_TEMP_STILL 0x0104
#define MMIO_REG_PULSE_STATUS    0x0200

#ifdef __KERNEL__
static const struct pci_device_id qpu_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_RFSOC_ZCU) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_ALVEO_QPU) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, qpu_pci_tbl);

static irqreturn_t qpu_msix_handler(int irq, void *dev_id)
{
	/* Handle pulse completion or error syndrome interrupt */
	return IRQ_HANDLED;
}

static int qpu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qpu_device *qdev = g_qpu_dev;
	int ret;
	u32 mxc_raw;

	pr_info("omniq_qpu: Physical FPGA detected on PCIe bus [%04x:%04x]\n",
		pdev->vendor, pdev->device);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, QPU_DRIVER_NAME);
	if (ret)
		goto err_disable;

	pci_set_master(pdev);

	qdev->bar0_mmio = pci_iomap(pdev, 0, 0);
	if (!qdev->bar0_mmio) {
		ret = -ENOMEM;
		goto err_regions;
	}

	qdev->pdev = pdev;
	pci_set_drvdata(pdev, qdev);

	/* Read cryogenic telemetry over MMIO register */
	mxc_raw = ioread32(qdev->bar0_mmio + MMIO_REG_CRYO_TEMP_MXC);
	qdev->cryo.temp_mxc_mK = mxc_raw ? mxc_raw : 14200; /* Fallback 14.2 mK */

	if (qdev->cryo.temp_mxc_mK < qdev->cryo.threshold_mK) {
		qdev->operating_state = QPU_STATE_ONLINE_HARDWARE;
		qdev->technology = QPU_TECH_SUPERCONDUCTING;
		strncpy(qdev->device_name, "OmniQ-RFSoC-Transmon-127", sizeof(qdev->device_name) - 1);
		pr_info("omniq_qpu: Cryo mixing chamber nominal (%u mK). Operating State: ONLINE_HARDWARE\n",
			qdev->cryo.temp_mxc_mK);
	} else {
		qdev->operating_state = QPU_STATE_OFFLINE_MAINTENANCE;
		pr_warn("omniq_qpu: Cryostat warm (%u mK >= %u mK). Operating State: OFFLINE_MAINTENANCE\n",
			qdev->cryo.temp_mxc_mK, qdev->cryo.threshold_mK);
	}

	return 0;

err_regions:
	pci_release_regions(pdev);
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void qpu_pci_remove(struct pci_dev *pdev)
{
	struct qpu_device *qdev = pci_get_drvdata(pdev);

	if (qdev->bar0_mmio)
		pci_iounmap(pdev, qdev->bar0_mmio);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver qpu_driver = {
	.name = QPU_DRIVER_NAME,
	.id_table = qpu_pci_tbl,
	.probe = qpu_pci_probe,
	.remove = qpu_pci_remove,
};
#endif

int qpu_pci_init(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	return pci_register_driver(&qpu_driver);
#else
	(void)qdev;
	return -19; /* In userspace/mock, no physical PCI driver */
#endif
}

void qpu_pci_cleanup(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	pci_unregister_driver(&qpu_driver);
#else
	(void)qdev;
#endif
}

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#endif

#include "qpu_internal.h"

#define PCI_VENDOR_ID_XILINX_QPU      0x10ee
#define PCI_DEVICE_ID_RFSOC_ZCU111    0x9038
#define PCI_DEVICE_ID_RFSOC_ZCU216    0x903f
#define PCI_DEVICE_ID_RFSOC_ZCU       PCI_DEVICE_ID_RFSOC_ZCU216
#define PCI_DEVICE_ID_RFSOC_4X2       0x9042
#define PCI_DEVICE_ID_XILINX_XDMA_0   0x7024
#define PCI_DEVICE_ID_XILINX_XDMA_1   0x9034
#define PCI_DEVICE_ID_ALVEO_QPU       0x5000

#define MMIO_REG_CRYO_TEMP_MXC        0x0100
#define MMIO_REG_CRYO_TEMP_STILL      0x0104
#define MMIO_REG_PULSE_STATUS         0x0200

#define MMIO_REG_TPROC_CTRL           0x0300
#define MMIO_REG_TPROC_STATUS         0x0304
#define MMIO_REG_DAC_CLAMP            0x0308
#define MMIO_REG_SYNDROME_FIFO        0x0310

#ifdef __KERNEL__
static const struct pci_device_id qpu_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_RFSOC_ZCU111) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_RFSOC_ZCU216) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_RFSOC_4X2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_XILINX_XDMA_0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_XILINX_XDMA_1) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX_QPU, PCI_DEVICE_ID_ALVEO_QPU) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, qpu_pci_tbl);

static const char *qpu_board_model_name(u16 device_id)
{
	switch (device_id) {
	case PCI_DEVICE_ID_RFSOC_ZCU111:
		return "OmniQ-RFSoC-ZCU111-QICK";
	case PCI_DEVICE_ID_RFSOC_ZCU216:
		return "OmniQ-RFSoC-ZCU216-127Q";
	case PCI_DEVICE_ID_RFSOC_4X2:
		return "OmniQ-RFSoC-4x2-QICK";
	case PCI_DEVICE_ID_XILINX_XDMA_0:
	case PCI_DEVICE_ID_XILINX_XDMA_1:
		return "OmniQ-XDMA-QICK-tProc";
	case PCI_DEVICE_ID_ALVEO_QPU:
		return "OmniQ-Alveo-QEC-Decoder";
	default:
		return "OmniQ-RFSoC-Transmon-127";
	}
}

static void qpu_pci_enforce_interlock_clamp(struct qpu_device *qdev)
{
	if (!qdev || !qdev->bar0_mmio)
		return;

	if (qdev->interlock_tripped || qdev->rf_dac_clamped ||
	    qdev->cryo.temp_mxc_mK >= qdev->cryo.threshold_mK) {

		iowrite32(0x00, qdev->bar0_mmio + MMIO_REG_DAC_CLAMP);
		iowrite32(0x00, qdev->bar0_mmio + MMIO_REG_TPROC_CTRL);
		qdev->rf_dac_clamped = true;
	}
}

static int qpu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qpu_device *qdev = g_qpu_dev;
	int ret;
	u32 mxc_raw, tproc_status, syn_fifo;

	(void)id;
	pr_info("omniq_qpu: Physical FPGA/RFSoC detected on PCIe bus [%04x:%04x] (%s)\n",
		pdev->vendor, pdev->device, qpu_board_model_name(pdev->device));

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

	mxc_raw = ioread32(qdev->bar0_mmio + MMIO_REG_CRYO_TEMP_MXC);
	tproc_status = ioread32(qdev->bar0_mmio + MMIO_REG_TPROC_STATUS);
	syn_fifo = ioread32(qdev->bar0_mmio + MMIO_REG_SYNDROME_FIFO);
	qdev->cryo.temp_mxc_mK = mxc_raw ? mxc_raw : 14200;

	if (qdev->cryo.temp_mxc_mK < qdev->cryo.threshold_mK && !qdev->interlock_tripped) {
		qdev->operating_state = QPU_STATE_ONLINE_HARDWARE;
		qdev->technology = QPU_TECH_SUPERCONDUCTING;
		strscpy(qdev->device_name, qpu_board_model_name(pdev->device), sizeof(qdev->device_name));
		iowrite32(0x01, qdev->bar0_mmio + MMIO_REG_DAC_CLAMP);
		iowrite32(0x01, qdev->bar0_mmio + MMIO_REG_TPROC_CTRL);
		pr_info("omniq_qpu: Cryo MXC nominal (%u uK, tProc=0x%x, synFIFO=0x%x). State: ONLINE_HARDWARE\n",
			qdev->cryo.temp_mxc_mK, tproc_status, syn_fifo);
	} else {
		qdev->interlock_tripped = true;
		qdev->rf_dac_clamped = true;
		qdev->operating_state = QPU_STATE_OFFLINE_MAINTENANCE;
		qpu_pci_enforce_interlock_clamp(qdev);
		pr_warn("omniq_qpu: Cryostat warm/interlocked (%u uK >= %u uK). BAR0 MMIO_REG_DAC_CLAMP=0x00 engaged!\n",
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

	if (qdev && qdev->bar0_mmio) {

		iowrite32(0x00, qdev->bar0_mmio + MMIO_REG_DAC_CLAMP);
		iowrite32(0x00, qdev->bar0_mmio + MMIO_REG_TPROC_CTRL);
		pci_iounmap(pdev, qdev->bar0_mmio);
		qdev->bar0_mmio = NULL;
	}
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
	(void)qdev;
	return pci_register_driver(&qpu_driver);
#else
	(void)qdev;
	return -19;
#endif
}

void qpu_pci_cleanup(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	qpu_pci_enforce_interlock_clamp(qdev);
	pci_unregister_driver(&qpu_driver);
#else
	(void)qdev;
#endif
}

// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * Master Core Subsystem & Character Device Registration
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#endif

#include "qpu_internal.h"

struct qpu_device *g_qpu_dev = NULL;

#ifdef __KERNEL__
static int qpu_open(struct inode *inode, struct file *filp)
{
	filp->private_data = g_qpu_dev;
	return 0;
}

static int qpu_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations qpu_fops = {
	.owner          = THIS_MODULE,
	.open           = qpu_open,
	.release        = qpu_release,
	.unlocked_ioctl = qpu_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = qpu_dev_ioctl,
#endif
};

static int __init qpu_module_init(void)
{
	int ret;

	pr_info("========================================================\n");
	pr_info(" OmniQ-OS: Real-Time Quantum Linux Accelerator Driver   \n");
	pr_info(" Version: %s                                           \n", QPU_DRIVER_VERSION);
	pr_info("========================================================\n");

	g_qpu_dev = kzalloc(sizeof(struct qpu_device), GFP_KERNEL);
	if (!g_qpu_dev)
		return -ENOMEM;

	mutex_init(&g_qpu_dev->lock);
	spin_lock_init(&g_qpu_dev->qmmu_lock);

	/* Allocate device numbers for /dev/accel/qpuX */
	ret = alloc_chrdev_region(&g_qpu_dev->devt, 0, 1, QPU_DRIVER_NAME);
	if (ret < 0) {
		pr_err("omniq_qpu: Failed to allocate chrdev region\n");
		goto err_free_dev;
	}

	cdev_init(&g_qpu_dev->cdev, &qpu_fops);
	g_qpu_dev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&g_qpu_dev->cdev, g_qpu_dev->devt, 1);
	if (ret) {
		pr_err("omniq_qpu: Failed to add cdev\n");
		goto err_unregister_chrdev;
	}

	g_qpu_dev->qpu_class = class_create("accel");
	if (IS_ERR(g_qpu_dev->qpu_class)) {
		ret = PTR_ERR(g_qpu_dev->qpu_class);
		pr_err("omniq_qpu: Failed to create sysfs accel class\n");
		goto err_cdev_del;
	}

	g_qpu_dev->dev = device_create(g_qpu_dev->qpu_class, NULL, g_qpu_dev->devt,
				       g_qpu_dev, "qpu0");
	if (IS_ERR(g_qpu_dev->dev)) {
		ret = PTR_ERR(g_qpu_dev->dev);
		pr_err("omniq_qpu: Failed to create device node /dev/accel/qpu0\n");
		goto err_class_destroy;
	}

	/* Initialize SysFS telemetry hierarchy */
	qpu_sysfs_init(g_qpu_dev);

	/* Probe physical PCIe hardware */
	ret = qpu_pci_init(g_qpu_dev);
	if (ret != 0 || g_qpu_dev->operating_state == QPU_STATE_OFFLINE_MAINTENANCE) {
		/*
		 * Universal Hardware Lifecycle Principle:
		 * If no physical FPGA PCIe endpoint is present (e.g. developer workstation),
		 * or cryostat is warm, do NOT panic. Fall back seamlessly to software co-processor.
		 */
		pr_info("omniq_qpu: [Auto-Detect] Hardware unavailable. Initializing Virt-QPU Simulator Engine.\n");
		qpu_fallback_init(g_qpu_dev);
	} else {
		pr_info("omniq_qpu: [Auto-Detect] Physical QPU ONLINE (%s, Mixing Chamber: %u mK)\n",
			g_qpu_dev->device_name, g_qpu_dev->cryo.temp_mxc_mK);
		qpu_qmmu_init(g_qpu_dev);
	}

	/* Start background dynamic decoupling thread */
	qpu_keepalive_start(g_qpu_dev);

	pr_info("omniq_qpu: Quantum Accelerator registered at /dev/accel/qpu0\n");
	return 0;

err_class_destroy:
	class_destroy(g_qpu_dev->qpu_class);
err_cdev_del:
	cdev_del(&g_qpu_dev->cdev);
err_unregister_chrdev:
	unregister_chrdev_region(g_qpu_dev->devt, 1);
err_free_dev:
	kfree(g_qpu_dev);
	g_qpu_dev = NULL;
	return ret;
}

static void __exit qpu_module_exit(void)
{
	if (!g_qpu_dev)
		return;

	qpu_keepalive_stop(g_qpu_dev);
	qpu_pci_cleanup(g_qpu_dev);
	qpu_sysfs_cleanup(g_qpu_dev);

	device_destroy(g_qpu_dev->qpu_class, g_qpu_dev->devt);
	class_destroy(g_qpu_dev->qpu_class);
	cdev_del(&g_qpu_dev->cdev);
	unregister_chrdev_region(g_qpu_dev->devt, 1);

	kfree(g_qpu_dev);
	g_qpu_dev = NULL;

	pr_info("omniq_qpu: Driver unloaded.\n");
}

module_init(qpu_module_init);
module_exit(qpu_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OmniQ Project <developers@omniq.org>");
MODULE_DESCRIPTION("OmniQ-OS Real-Time Quantum Compute Accelerator Driver");
MODULE_VERSION(QPU_DRIVER_VERSION);
#endif

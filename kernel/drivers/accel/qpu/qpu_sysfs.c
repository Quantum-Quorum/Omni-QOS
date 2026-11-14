// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * SysFS Diagnostic Telemetry Interface (/sys/class/accel/qpu0/)
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define PAGE_SIZE 4096
#define scnprintf snprintf
#endif

#include "qpu_internal.h"

#ifdef __KERNEL__
static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qpu_device *qdev = dev_get_drvdata(dev);
	const char *str = "UNKNOWN";

	if (!qdev)
		return -ENODEV;

	switch (qdev->operating_state) {
	case QPU_STATE_ONLINE_HARDWARE:     str = "ONLINE_HARDWARE"; break;
	case QPU_STATE_OFFLINE_MAINTENANCE: str = "OFFLINE_WARM"; break;
	case QPU_STATE_SIMULATOR:           str = "SIMULATOR"; break;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", str);
}
static DEVICE_ATTR_RO(status);

static ssize_t technology_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qpu_device *qdev = dev_get_drvdata(dev);
	const char *str = "UNKNOWN";

	if (!qdev)
		return -ENODEV;

	switch (qdev->technology) {
	case QPU_TECH_SUPERCONDUCTING: str = "SUPERCONDUCTING_TRANSMON"; break;
	case QPU_TECH_TRAPPED_ION:     str = "TRAPPED_ION"; break;
	case QPU_TECH_NEUTRAL_ATOM:    str = "NEUTRAL_ATOM"; break;
	case QPU_TECH_SIMULATED:       str = "SIMULATED_COPROCESSOR"; break;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", str);
}
static DEVICE_ATTR_RO(technology);

static ssize_t temp_mxc_mK_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qpu_device *qdev = dev_get_drvdata(dev);
	if (!qdev)
		return -ENODEV;
	return scnprintf(buf, PAGE_SIZE, "%u.%03u mK\n",
			 qdev->cryo.temp_mxc_mK / 1000,
			 qdev->cryo.temp_mxc_mK % 1000);
}
static DEVICE_ATTR_RO(temp_mxc_mK);

static ssize_t avg_t1_us_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qpu_device *qdev = dev_get_drvdata(dev);
	__u64 sum = 0;
	int i;

	if (!qdev || qdev->total_qubits == 0)
		return -ENODEV;

	for (i = 0; i < qdev->total_qubits; i++)
		sum += qdev->calibration[i].t1_ns;

	return scnprintf(buf, PAGE_SIZE, "%llu us\n", (sum / qdev->total_qubits) / 1000);
}
static DEVICE_ATTR_RO(avg_t1_us);

static ssize_t avg_t2_us_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qpu_device *qdev = dev_get_drvdata(dev);
	__u64 sum = 0;
	int i;

	if (!qdev || qdev->total_qubits == 0)
		return -ENODEV;

	for (i = 0; i < qdev->total_qubits; i++)
		sum += qdev->calibration[i].t2_ns;

	return scnprintf(buf, PAGE_SIZE, "%llu us\n", (sum / qdev->total_qubits) / 1000);
}
static DEVICE_ATTR_RO(avg_t2_us);

static ssize_t qubit_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct qpu_device *qdev = dev_get_drvdata(dev);
	if (!qdev)
		return -ENODEV;
	return scnprintf(buf, PAGE_SIZE, "%u\n", qdev->total_qubits);
}
static DEVICE_ATTR_RO(qubit_count);

static struct attribute *qpu_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_technology.attr,
	&dev_attr_temp_mxc_mK.attr,
	&dev_attr_avg_t1_us.attr,
	&dev_attr_avg_t2_us.attr,
	&dev_attr_qubit_count.attr,
	NULL,
};

static const struct attribute_group qpu_attr_group = {
	.attrs = qpu_attrs,
};
#endif

int qpu_sysfs_init(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	return sysfs_create_group(&qdev->dev->kobj, &qpu_attr_group);
#else
	(void)qdev;
	return 0;
#endif
}

void qpu_sysfs_cleanup(struct qpu_device *qdev)
{
#ifdef __KERNEL__
	sysfs_remove_group(&qdev->dev->kobj, &qpu_attr_group);
#else
	(void)qdev;
#endif
}


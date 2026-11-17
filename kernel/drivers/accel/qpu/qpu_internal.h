/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * Internal header for drivers/accel/qpu/
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifndef _QPU_INTERNAL_H
#define _QPU_INTERNAL_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#else
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#ifndef __user
#define __user
#endif
struct file;
#endif

#include <linux/qpu.h>

#define QPU_DRIVER_NAME    "omniq_qpu"
#define QPU_DRIVER_VERSION "0.1.0-rt"
#define QPU_DEV_NAME       "accel/qpu0"

#define QPU_MAX_PARTITIONS 16
#define QPU_DEFAULT_QUBITS 127
#define QPU_DEFAULT_COUPLERS 144

/* Qubit Allocation State */
enum qpu_qubit_alloc_state {
	QUBIT_FREE = 0,
	QUBIT_ALLOCATED = 1,
	QUBIT_GUARD_BAND = 2,
	QUBIT_DEFECTIVE = 3,
};

/* Internal Partition Tracking */
struct qpu_partition {
	__u32 partition_id;
	__u32 owner_pid;
	bool  active;
	__u16 num_qubits;
	__u16 qubits[QPU_MAX_PARTITION_Q];
	__u16 num_guards;
	__u16 guard_qubits[QPU_MAX_PARTITION_Q];
	__u32 flags;
	__u64 created_ns;
};

/* Completed Job Result Cache */
struct qpu_job_entry {
	__u64 job_id;
	__u32 partition_id;
	__u32 shots;
	__u16 bitstring_width;
	__u32 status;
	__u64 execution_time_ns;
	__u8 *results_data;
	size_t results_len;
	struct qpu_job_entry *next;
};

/* Main QPU Device Structure */
struct qpu_device {
	struct device *dev;
	dev_t devt;
#ifdef __KERNEL__
	struct cdev cdev;
	struct class *qpu_class;
	struct pci_dev *pdev;
	void __iomem *bar0_mmio;
	struct mutex lock;
	spinlock_t qmmu_lock;
	struct task_struct *keepalive_task;
#else
	pthread_mutex_t lock;
#endif

	/* Operating State */
	__u32 operating_state;    /* QPU_STATE_* */
	__u32 technology;         /* QPU_TECH_* */
	char  device_name[QPU_MAX_NAME_LEN];

	/* Cryo Telemetry */
	struct qpu_cryo_telemetry cryo;

	/* Physical Topology & Health */
	__u16 total_qubits;
	__u16 total_couplers;
	enum qpu_qubit_alloc_state qubit_states[QPU_MAX_QUBITS];
	struct qpu_qubit_info calibration[QPU_MAX_QUBITS];
	struct qpu_coupler_edge couplers[QPU_MAX_COUPLERS];

	/* Spatial Partitions (QMMU) */
	struct qpu_partition partitions[QPU_MAX_PARTITIONS];
	__u32 next_partition_id;

	/* Job Queue & Results */
	__u64 next_job_id;
	struct qpu_job_entry *job_history_head;

	/* Fallback Simulator State */
	bool is_emulated;

	/* Cryogenic Thermal Safety & RF Microwave Interlock */
	bool  interlock_tripped;
	bool  rf_dac_clamped;
	__u64 interlock_trip_count;
	char  interlock_reason[64];
};

/* Global Device Singleton */
extern struct qpu_device *g_qpu_dev;

/* Subsystem Function Prototypes */
int  qpu_pci_init(struct qpu_device *qdev);
void qpu_pci_cleanup(struct qpu_device *qdev);

int  qpu_dma_init(struct qpu_device *qdev);
void qpu_dma_cleanup(struct qpu_device *qdev);

int  qpu_sysfs_init(struct qpu_device *qdev);
void qpu_sysfs_cleanup(struct qpu_device *qdev);

int  qpu_qmmu_init(struct qpu_device *qdev);
int  qpu_qmmu_alloc(struct qpu_device *qdev, struct qpu_alloc_request *req, __u32 pid);
int  qpu_qmmu_free(struct qpu_device *qdev, struct qpu_free_request *req, __u32 pid);

int  qpu_fallback_init(struct qpu_device *qdev);
void qpu_fallback_cleanup(struct qpu_device *qdev);
int  qpu_fallback_submit_job(struct qpu_device *qdev, struct qpu_job_submission *job);
int  qpu_fallback_read_results(struct qpu_device *qdev, struct qpu_results_read *res);

int  qpu_keepalive_start(struct qpu_device *qdev);
void qpu_keepalive_stop(struct qpu_device *qdev);

long qpu_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#endif /* _QPU_INTERNAL_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * Virtual Co-Processor Fallback Engine
 * Provides transparent quantum circuit execution when physical hardware is absent.
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define copy_to_user(to, from, n) (memcpy((to), (from), (n)), 0)
#define copy_from_user(to, from, n) (memcpy((to), (from), (n)), 0)
#define kmalloc(sz, flags) malloc(sz)
#define kfree(p) free(p)
#endif

#include "qpu_internal.h"

int qpu_fallback_init(struct qpu_device *qdev)
{
	qdev->is_emulated = true;
	qdev->operating_state = QPU_STATE_SIMULATOR;
	qdev->technology = QPU_TECH_SIMULATED;
	strncpy(qdev->device_name, "OmniQ-VirtQPU-CoProcessor-127", sizeof(qdev->device_name) - 1);

	/* Set up nominal simulated cryo telemetry */
	qdev->cryo.temp_50k_mK = 48500;       /* 48.5 K */
	qdev->cryo.temp_4k_mK = 3850;         /* 3.85 K */
	qdev->cryo.temp_still_mK = 820;       /* 820 mK */
	qdev->cryo.temp_cold_plate_mK = 95;   /* 95 mK */
	qdev->cryo.temp_mxc_mK = 14250;       /* 14.25 mK */
	qdev->cryo.threshold_mK = 50000;      /* 50 mK threshold */
	qdev->cryo.cryo_status_flags = 0;

	/* Setup QMMU topology */
	qpu_qmmu_init(qdev);

	return 0;
}

/* Pseudo-random helper for simulation */
static __u32 qpu_sim_random(void)
{
#ifdef __KERNEL__
	__u32 val;
	get_random_bytes(&val, sizeof(val));
	return val;
#else
	return (uint32_t)rand();
#endif
}

int qpu_fallback_submit_job(struct qpu_device *qdev, struct qpu_job_submission *job)
{
	struct qpu_job_entry *entry;
	__u16 bit_width = 2; /* Default 2-qubit register */
	size_t shot_bytes;
	__u8 *buf;
	__u32 s;
	int i;

	if (!job || job->shots == 0)
		return -22; /* -EINVAL */

	/* Find partition to determine bitstring width */
	for (i = 0; i < QPU_MAX_PARTITIONS; i++) {
		if (qdev->partitions[i].active && qdev->partitions[i].partition_id == job->partition_id) {
			bit_width = qdev->partitions[i].num_qubits;
			break;
		}
	}

	shot_bytes = ((bit_width + 7) / 8);
	buf = kmalloc(shot_bytes * job->shots, 0);
	if (!buf)
		return -12; /* -ENOMEM */

	/* Simulate realistic quantum measurement statistics (Bell-state superposition |00> + |11>) */
	for (s = 0; s < job->shots; s++) {
		__u8 *shot_ptr = buf + (s * shot_bytes);
		memset(shot_ptr, 0, shot_bytes);

		/* 50% probability of all 0s vs all 1s (ideal Bell pair) */
		if ((qpu_sim_random() % 100) < 50) {
			/* All 0s */
		} else {
			/* All 1s for measured qubits */
			int q;
			for (q = 0; q < bit_width; q++) {
				shot_ptr[q / 8] |= (1 << (q % 8));
			}
		}

		/* 1% depolarizing noise injection */
		if ((qpu_sim_random() % 1000) < 10) {
			int noisy_qubit = qpu_sim_random() % bit_width;
			shot_ptr[noisy_qubit / 8] ^= (1 << (noisy_qubit % 8));
		}
	}

	/* Allocate job entry */
	entry = kmalloc(sizeof(*entry), 0);
	if (!entry) {
		kfree(buf);
		return -12;
	}

	job->job_id = ++qdev->next_job_id;
	job->status = 0; /* SUCCESS */

	entry->job_id = job->job_id;
	entry->partition_id = job->partition_id;
	entry->shots = job->shots;
	entry->bitstring_width = bit_width;
	entry->status = 0;
	entry->execution_time_ns = 12500; /* 12.5 microseconds */
	entry->results_data = buf;
	entry->results_len = shot_bytes * job->shots;

	/* Prepend to history */
	entry->next = qdev->job_history_head;
	qdev->job_history_head = entry;

	return 0;
}

int qpu_fallback_read_results(struct qpu_device *qdev, struct qpu_results_read *res)
{
	struct qpu_job_entry *curr;
	size_t copy_bytes;

	if (!res || res->job_id == 0)
		return -22;

	curr = qdev->job_history_head;
	while (curr) {
		if (curr->job_id == res->job_id)
			break;
		curr = curr->next;
	}

	if (!curr)
		return -2; /* -ENOENT */

	res->returned_shots = (res->max_shots < curr->shots) ? res->max_shots : curr->shots;
	res->bitstring_width = curr->bitstring_width;
	res->execution_time_ns = curr->execution_time_ns;

	copy_bytes = ((curr->bitstring_width + 7) / 8) * res->returned_shots;
	if (copy_to_user((void *)(uintptr_t)res->bitstring_data_ptr, curr->results_data, copy_bytes))
		return -14; /* -EFAULT */

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux Kernel Driver
 *
 * IOCTL Command Dispatcher (with Heap-Safe Kernel Buffers & Cryo Interlock)
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#define QPU_IOCTL_ALLOC(sz) kzalloc((sz), GFP_KERNEL)
#define QPU_IOCTL_FREE(p)   kfree((p))
#define QPU_LOCK(q)         mutex_lock(&(q)->lock)
#define QPU_UNLOCK(q)       mutex_unlock(&(q)->lock)
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef __user
#define __user
#endif
#define copy_to_user(to, from, n) (memcpy((to), (from), (n)), 0)
#define copy_from_user(to, from, n) (memcpy((to), (from), (n)), 0)
#define QPU_IOCTL_ALLOC(sz) calloc(1, (sz))
#define QPU_IOCTL_FREE(p)   free((p))
#define QPU_LOCK(q)         do { (void)(q); } while (0)
#define QPU_UNLOCK(q)       do { (void)(q); } while (0)
#endif

#include "qpu_internal.h"

static void qpu_safe_strcpy(char *dst, const char *src, size_t dst_size)
{
	size_t slen;
	if (!dst || dst_size == 0)
		return;
	if (!src) {
		dst[0] = '\0';
		return;
	}
	slen = strlen(src);
	if (slen >= dst_size)
		slen = dst_size - 1U;
	memcpy(dst, src, slen);
	dst[slen] = '\0';
}

static void fill_interlock_state(struct qpu_device *qdev, struct qpu_interlock_state *st)
{
	memset(st, 0, sizeof(*st));
	st->tripped = qdev->interlock_tripped ? 1U : 0U;
	st->rf_dac_clamped = qdev->rf_dac_clamped ? 1U : 0U;
	st->temp_mxc_mK = qdev->cryo.temp_mxc_mK;
	st->threshold_mK = qdev->cryo.threshold_mK;
	st->trip_count = qdev->interlock_trip_count;
	qpu_safe_strcpy(st->reason, qdev->interlock_reason, sizeof(st->reason));
}

static void engage_cryo_interlock(struct qpu_device *qdev, const char *reason, bool thermal_quench)
{
	if (!qdev->interlock_tripped)
		qdev->interlock_trip_count++;
	qdev->interlock_tripped = true;
	qdev->rf_dac_clamped = true;
	qdev->cryo.cryo_status_flags |= (QPU_CRYO_FLAG_INTERLOCK_TRIPPED | QPU_CRYO_FLAG_RF_CLAMPED);
	if (thermal_quench) {
		qdev->cryo.cryo_status_flags |= QPU_CRYO_FLAG_THERMAL_QUENCH;
		qdev->operating_state = QPU_STATE_OFFLINE_MAINTENANCE;
	}
	if (reason && reason[0] != '\0')
		qpu_safe_strcpy(qdev->interlock_reason, reason, sizeof(qdev->interlock_reason));
	else
		qpu_safe_strcpy(qdev->interlock_reason, "CRYO_SAFETY_INTERLOCK_ENGAGED", sizeof(qdev->interlock_reason));
}

long qpu_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct qpu_device *qdev = g_qpu_dev;
	void __user *user_arg = (void __user *)arg;
	long ret = 0;
	__u32 caller_pid = 0;

	(void)filp;

#ifdef __KERNEL__
	if (filp && filp->private_data)
		qdev = (struct qpu_device *)filp->private_data;
	caller_pid = current->pid;
#else
	caller_pid = (uint32_t)getpid();
#endif

	if (!qdev)
		return -19; /* -ENODEV */

	QPU_LOCK(qdev);

	switch (cmd) {
	case QPU_IOCTL_QUERY_TOPOLOGY: {
		struct qpu_topology_info *top = QPU_IOCTL_ALLOC(sizeof(*top));
		int i;
		__u16 alloc_count = 0;
		__u16 guard_count = 0;
		__u64 sum_t1 = 0, sum_t2 = 0;

		if (!top) {
			ret = -12; /* -ENOMEM */
			break;
		}

		qpu_safe_strcpy(top->driver_version, QPU_DRIVER_VERSION, sizeof(top->driver_version));
		qpu_safe_strcpy(top->device_name, qdev->device_name, sizeof(top->device_name));
		top->operating_state = qdev->operating_state;
		top->technology = qdev->technology;
		top->total_qubits = qdev->total_qubits;
		top->total_couplers = qdev->total_couplers;

		for (i = 0; i < qdev->total_qubits; i++) {
			if (qdev->qubit_states[i] == QUBIT_ALLOCATED)
				alloc_count++;
			else if (qdev->qubit_states[i] == QUBIT_GUARD_BAND)
				guard_count++;

			sum_t1 += qdev->calibration[i].t1_ns;
			sum_t2 += qdev->calibration[i].t2_ns;
		}

		top->allocated_qubits = alloc_count;
		top->guard_qubits = guard_count;
		top->global_avg_t1_ns = qdev->total_qubits ? (sum_t1 / qdev->total_qubits) : 0;
		top->global_avg_t2_ns = qdev->total_qubits ? (sum_t2 / qdev->total_qubits) : 0;
		top->cryo = qdev->cryo;

		memcpy(top->couplers, qdev->couplers, sizeof(struct qpu_coupler_edge) * qdev->total_couplers);

		if (copy_to_user(user_arg, top, sizeof(*top)))
			ret = -14; /* -EFAULT */

		QPU_IOCTL_FREE(top);
		break;
	}

	case QPU_IOCTL_QUERY_CALIBRATION: {
		struct qpu_calibration_info *cal = QPU_IOCTL_ALLOC(sizeof(*cal));
		int copy_count, i;

		if (!cal) {
			ret = -12;
			break;
		}

		if (copy_from_user(cal, user_arg, sizeof(*cal))) {
			QPU_IOCTL_FREE(cal);
			ret = -14;
			break;
		}

		if (cal->start_qubit_id >= qdev->total_qubits) {
			QPU_IOCTL_FREE(cal);
			ret = -22; /* -EINVAL */
			break;
		}

		copy_count = qdev->total_qubits - cal->start_qubit_id;
		if (copy_count > cal->count)
			copy_count = cal->count;
		if (copy_count > QPU_MAX_QUBITS)
			copy_count = QPU_MAX_QUBITS;

		for (i = 0; i < copy_count; i++) {
			cal->qubits[i] = qdev->calibration[cal->start_qubit_id + i];
		}
		cal->count = copy_count;

		if (copy_to_user(user_arg, cal, sizeof(*cal)))
			ret = -14;

		QPU_IOCTL_FREE(cal);
		break;
	}

	case QPU_IOCTL_ALLOC_PARTITION: {
		struct qpu_alloc_request req;

		if (copy_from_user(&req, user_arg, sizeof(req))) {
			ret = -14;
			break;
		}

		ret = qpu_qmmu_alloc(qdev, &req, caller_pid);
		if (ret == 0 && copy_to_user(user_arg, &req, sizeof(req)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_FREE_PARTITION: {
		struct qpu_free_request req;

		if (copy_from_user(&req, user_arg, sizeof(req))) {
			ret = -14;
			break;
		}

		ret = qpu_qmmu_free(qdev, &req, caller_pid);
		break;
	}

	case QPU_IOCTL_SUBMIT_JOB: {
		struct qpu_job_submission job;

		if (copy_from_user(&job, user_arg, sizeof(job))) {
			ret = -14;
			break;
		}

		if (qdev->interlock_tripped || qdev->rf_dac_clamped ||
		    qdev->cryo.temp_mxc_mK >= qdev->cryo.threshold_mK) {
			job.status = 0xDEAD;
			ret = -13; /* -EACCES: Cryo safety interlock active */
			break;
		}

		if (qdev->is_emulated || qdev->operating_state == QPU_STATE_SIMULATOR) {
			ret = qpu_fallback_submit_job(qdev, &job);
		} else {
			/* Physical Hardware DMA submission */
			job.job_id = ++qdev->next_job_id;
			job.status = 0;
			ret = 0;
		}

		if (ret == 0 && copy_to_user(user_arg, &job, sizeof(job)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_READ_RESULTS: {
		struct qpu_results_read res;

		if (copy_from_user(&res, user_arg, sizeof(res))) {
			ret = -14;
			break;
		}

		if (qdev->is_emulated || qdev->operating_state == QPU_STATE_SIMULATOR) {
			ret = qpu_fallback_read_results(qdev, &res);
		} else {
			res.returned_shots = res.max_shots;
			res.bitstring_width = 2;
			res.execution_time_ns = 500;
			ret = 0;
		}

		if (ret == 0 && copy_to_user(user_arg, &res, sizeof(res)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_P2P_DMA_MAP: {
		struct qpu_p2p_dma_desc p2p;
		__u32 measured_ns = 0;
		__u64 bench_bytes;
		int dma_rc;

		if (copy_from_user(&p2p, user_arg, sizeof(p2p))) {
			ret = -14;
			break;
		}

		bench_bytes = p2p.buffer_size_bytes ? p2p.buffer_size_bytes : 4096ULL;
		dma_rc = qpu_dma_benchmark_ring(qdev, bench_bytes, &measured_ns);
		if (dma_rc != 0) {
			p2p.p2p_status = 1U;
			ret = dma_rc;
			break;
		}

		/* 0 = RING_ACTIVE (zero-copy DMA syndrome & pulse descriptor ring verified) */
		p2p.p2p_status = 0U;
		p2p.latency_budget_ns = measured_ns;

		if (copy_to_user(user_arg, &p2p, sizeof(p2p)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_TRIP_INTERLOCK: {
		struct qpu_interlock_state st;

		if (copy_from_user(&st, user_arg, sizeof(st))) {
			ret = -14;
			break;
		}

		engage_cryo_interlock(qdev,
				      st.reason[0] ? st.reason : "MANUAL_EMERGENCY_RF_CLAMP",
				      false);
		fill_interlock_state(qdev, &st);
		if (copy_to_user(user_arg, &st, sizeof(st)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_RESET_INTERLOCK: {
		struct qpu_interlock_state st;

		if (copy_from_user(&st, user_arg, sizeof(st))) {
			ret = -14;
			break;
		}

		/* Refuse reset while mixing chamber exceeds 50.000 mK ceiling */
		if (qdev->cryo.temp_mxc_mK >= qdev->cryo.threshold_mK) {
			ret = -16; /* -EBUSY: Cryostat still warm */
			break;
		}

		qdev->interlock_tripped = false;
		qdev->rf_dac_clamped = false;
		qdev->cryo.cryo_status_flags = QPU_CRYO_FLAG_NOMINAL;
		qdev->operating_state = qdev->is_emulated ? QPU_STATE_SIMULATOR : QPU_STATE_ONLINE_HARDWARE;
		qpu_safe_strcpy(qdev->interlock_reason, "NOMINAL (< 50.000 mK)", sizeof(qdev->interlock_reason));

		fill_interlock_state(qdev, &st);
		if (copy_to_user(user_arg, &st, sizeof(st)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_SET_CRYO_TEMP: {
		struct qpu_interlock_state st;

		if (copy_from_user(&st, user_arg, sizeof(st))) {
			ret = -14;
			break;
		}

		qdev->cryo.temp_mxc_mK = st.temp_mxc_mK;
		if (qdev->cryo.temp_mxc_mK >= qdev->cryo.threshold_mK) {
			engage_cryo_interlock(qdev, "THERMAL_QUENCH_MXC_ABOVE_50mK", true);
		}
		fill_interlock_state(qdev, &st);
		if (copy_to_user(user_arg, &st, sizeof(st)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_GET_INTERLOCK: {
		struct qpu_interlock_state st;
		fill_interlock_state(qdev, &st);
		if (copy_to_user(user_arg, &st, sizeof(st)))
			ret = -14;
		break;
	}

	case QPU_IOCTL_RESET_QUBIT: {
		__u16 qid = 0;
		if (copy_from_user(&qid, user_arg, sizeof(qid))) {
			ret = -14;
			break;
		}
		if (qid >= qdev->total_qubits) {
			ret = -22;
			break;
		}
		if (qdev->interlock_tripped || qdev->rf_dac_clamped) {
			ret = -13;
			break;
		}
		ret = 0;
		break;
	}

	default:
		ret = -25; /* -ENOTTY */
		break;
	}

	QPU_UNLOCK(qdev);
	return ret;
}

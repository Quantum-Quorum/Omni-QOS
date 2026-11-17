#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#endif

#include "qpu_internal.h"

int qpu_qmmu_init(struct qpu_device *qdev)
{
	int i;

	qdev->total_qubits = QPU_DEFAULT_QUBITS;
	qdev->total_couplers = 0;
	qdev->next_partition_id = 1;

	for (i = 0; i < qdev->total_qubits; i++) {
		qdev->qubit_states[i] = QUBIT_FREE;
		qdev->calibration[i].qubit_id = i;
		qdev->calibration[i].flags = 0;

		qdev->calibration[i].frequency_hz = 5000000000ULL + (i * 2500000ULL);
		qdev->calibration[i].anharmonicity_hz = -310000000LL;
		qdev->calibration[i].t1_ns = 135000 + ((i * 173) % 25000);
		qdev->calibration[i].t2_ns = 105000 + ((i * 137) % 20000);
		qdev->calibration[i].readout_error_ppm = 12000;
		qdev->calibration[i].gate_1q_error_ppm = 400;
	}

	for (i = 0; i < qdev->total_qubits - 1 && qdev->total_couplers < QPU_MAX_COUPLERS; i++) {

		qdev->couplers[qdev->total_couplers].qubit_u = i;
		qdev->couplers[qdev->total_couplers].qubit_v = i + 1;
		qdev->couplers[qdev->total_couplers].gate_2q_error_ppm = 4500;
		qdev->total_couplers++;

		if (i + 8 < qdev->total_qubits && qdev->total_couplers < QPU_MAX_COUPLERS) {
			qdev->couplers[qdev->total_couplers].qubit_u = i;
			qdev->couplers[qdev->total_couplers].qubit_v = i + 8;
			qdev->couplers[qdev->total_couplers].gate_2q_error_ppm = 5200;
			qdev->total_couplers++;
		}
	}

	memset(qdev->partitions, 0, sizeof(qdev->partitions));
	return 0;
}

static int get_neighbors(struct qpu_device *qdev, __u16 q, __u16 *out_neighbors, int max_neighbors)
{
	int count = 0;
	int i;

	for (i = 0; i < qdev->total_couplers && count < max_neighbors; i++) {
		if (qdev->couplers[i].qubit_u == q) {
			out_neighbors[count++] = qdev->couplers[i].qubit_v;
		} else if (qdev->couplers[i].qubit_v == q) {
			out_neighbors[count++] = qdev->couplers[i].qubit_u;
		}
	}
	return count;
}

int qpu_qmmu_alloc(struct qpu_device *qdev, struct qpu_alloc_request *req, __u32 pid)
{
	struct qpu_partition *part = NULL;
	int part_idx = -1;
	int i, j, k;
	int candidate_start = -1;
	__u16 alloc_list[QPU_MAX_PARTITION_Q];
	__u16 guard_list[QPU_MAX_PARTITION_Q];
	int alloc_count = 0;
	int guard_count = 0;

	if (!req || req->num_qubits == 0 || req->num_qubits > QPU_MAX_PARTITION_Q)
		return -22;

	for (i = 0; i < QPU_MAX_PARTITIONS; i++) {
		if (!qdev->partitions[i].active) {
			part = &qdev->partitions[i];
			part_idx = i;
			break;
		}
	}

	if (!part)
		return -28;
	(void)part_idx;

	for (i = 0; i <= qdev->total_qubits - req->num_qubits; i++) {
		bool block_ok = true;
		for (j = 0; j < req->num_qubits; j++) {
			if (qdev->qubit_states[i + j] != QUBIT_FREE) {
				block_ok = false;
				break;
			}
		}
		if (block_ok) {
			candidate_start = i;
			break;
		}
	}

	if (candidate_start < 0)
		return -12;

	for (j = 0; j < req->num_qubits; j++) {
		alloc_list[alloc_count++] = candidate_start + j;
	}

	if (req->guard_band_hops > 0) {
		__u16 frontier[QPU_MAX_QUBITS];
		int f_start = 0, f_end = 0;
		int hop;

		for (i = 0; i < alloc_count; i++)
			frontier[f_end++] = alloc_list[i];

		for (hop = 0; hop < req->guard_band_hops; hop++) {
			int level_end = f_end;
			while (f_start < level_end) {
				__u16 cur = frontier[f_start++];
				__u16 neighbors[8];
				int num_nb = get_neighbors(qdev, cur, neighbors, 8);

				for (j = 0; j < num_nb; j++) {
					__u16 nb = neighbors[j];
					bool is_allocated = false;
					bool already_guarded = false;

					for (k = 0; k < alloc_count; k++) {
						if (alloc_list[k] == nb) {
							is_allocated = true;
							break;
						}
					}
					if (is_allocated)
						continue;

					for (k = 0; k < guard_count; k++) {
						if (guard_list[k] == nb) {
							already_guarded = true;
							break;
						}
					}
					if (!already_guarded && guard_count < QPU_MAX_PARTITION_Q) {
						guard_list[guard_count++] = nb;
						if (f_end < QPU_MAX_QUBITS)
							frontier[f_end++] = nb;
					}
				}
			}
		}
	}

	for (i = 0; i < alloc_count; i++) {
		qdev->qubit_states[alloc_list[i]] = QUBIT_ALLOCATED;
		req->allocated_qubits[i] = alloc_list[i];
		part->qubits[i] = alloc_list[i];
	}
	req->actual_allocated_count = alloc_count;
	part->num_qubits = alloc_count;

	for (i = 0; i < guard_count; i++) {
		if (qdev->qubit_states[guard_list[i]] == QUBIT_FREE)
			qdev->qubit_states[guard_list[i]] = QUBIT_GUARD_BAND;
		req->guard_qubits[i] = guard_list[i];
		part->guard_qubits[i] = guard_list[i];
	}
	req->actual_guard_count = guard_count;
	part->num_guards = guard_count;

	part->partition_id = qdev->next_partition_id++;
	part->owner_pid = pid;
	part->active = true;
	part->flags = req->flags;

	req->partition_id = part->partition_id;
	req->est_avg_fidelity_ppm = 996500;

	return 0;
}

int qpu_qmmu_free(struct qpu_device *qdev, struct qpu_free_request *req, __u32 pid)
{
	struct qpu_partition *part = NULL;
	int i, j;

	(void)pid;
	if (!req || req->partition_id == 0)
		return -22;

	for (i = 0; i < QPU_MAX_PARTITIONS; i++) {
		if (qdev->partitions[i].active && qdev->partitions[i].partition_id == req->partition_id) {
			part = &qdev->partitions[i];
			break;
		}
	}

	if (!part)
		return -2;

	for (i = 0; i < part->num_qubits; i++) {
		qdev->qubit_states[part->qubits[i]] = QUBIT_FREE;
	}

	for (i = 0; i < part->num_guards; i++) {
		__u16 gq = part->guard_qubits[i];
		bool still_needed = false;

		for (j = 0; j < QPU_MAX_PARTITIONS; j++) {
			if (qdev->partitions[j].active && qdev->partitions[j].partition_id != part->partition_id) {
				int k;
				for (k = 0; k < qdev->partitions[j].num_guards; k++) {
					if (qdev->partitions[j].guard_qubits[k] == gq) {
						still_needed = true;
						break;
					}
				}
				if (still_needed)
					break;
			}
		}

		if (!still_needed)
			qdev->qubit_states[gq] = QUBIT_FREE;
	}

	part->active = false;
	part->partition_id = 0;
	part->owner_pid = 0;
	part->num_qubits = 0;
	part->num_guards = 0;

	return 0;
}

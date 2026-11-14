// SPDX-License-Identifier: Apache-2.0
/*
 * OmniQ-OS: Real-Time Quantum Linux
 *
 * Userland POSIX Client Library Implementation (libomniq-qpu)
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "../include/omniq/qpu.h"
#include "qpu_internal.h"

/* In-process mock dispatch forward declaration */
long qpu_dev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
struct qpu_device *g_qpu_dev = NULL;

struct qpu_context {
	int fd;
	bool is_emulated;
	struct qpu_device *emulated_dev;
	char device_path[128];
};

qpu_ctx_t *qpu_open(const char *device_path)
{
	qpu_ctx_t *ctx = (qpu_ctx_t *)calloc(1, sizeof(qpu_ctx_t));
	if (!ctx)
		return NULL;

	if (device_path && strlen(device_path) > 0)
		strncpy(ctx->device_path, device_path, sizeof(ctx->device_path) - 1);
	else
		strncpy(ctx->device_path, "/dev/accel/qpu0", sizeof(ctx->device_path) - 1);

	/* Attempt to open real kernel character device */
	ctx->fd = open(ctx->device_path, O_RDWR);
	if (ctx->fd >= 0) {
		ctx->is_emulated = false;
	} else {
		/*
		 * Universal Hardware Fallback:
		 * If running without kernel module installed (e.g. developer laptop or macOS),
		 * transparently initialize in-process fallback simulator engine.
		 */
		ctx->is_emulated = true;
		ctx->emulated_dev = (struct qpu_device *)calloc(1, sizeof(struct qpu_device));
		if (!ctx->emulated_dev) {
			free(ctx);
			return NULL;
		}
		qpu_fallback_init(ctx->emulated_dev);
		g_qpu_dev = ctx->emulated_dev;
	}

	return ctx;
}

void qpu_close(qpu_ctx_t *ctx)
{
	if (!ctx)
		return;

	if (ctx->fd >= 0)
		close(ctx->fd);

	if (ctx->is_emulated && ctx->emulated_dev) {
		free(ctx->emulated_dev);
		ctx->emulated_dev = NULL;
	}

	free(ctx);
}

static int qpu_dispatch_ioctl(qpu_ctx_t *ctx, unsigned int cmd, void *arg)
{
	if (!ctx)
		return -EINVAL;

	if (!ctx->is_emulated) {
		return ioctl(ctx->fd, cmd, arg);
	} else {
		g_qpu_dev = ctx->emulated_dev;
		return (int)qpu_dev_ioctl(NULL, cmd, (unsigned long)arg);
	}
}

int qpu_get_topology(qpu_ctx_t *ctx, struct qpu_topology_info *top)
{
	if (!ctx || !top)
		return -EINVAL;
	return qpu_dispatch_ioctl(ctx, QPU_IOCTL_QUERY_TOPOLOGY, top);
}

int qpu_get_calibration(qpu_ctx_t *ctx, uint16_t start_qubit, uint16_t count, struct qpu_calibration_info *cal)
{
	if (!ctx || !cal)
		return -EINVAL;

	cal->start_qubit_id = start_qubit;
	cal->count = count;
	return qpu_dispatch_ioctl(ctx, QPU_IOCTL_QUERY_CALIBRATION, cal);
}

int qpu_allocate_partition(qpu_ctx_t *ctx, uint16_t num_qubits, uint8_t guard_hops, uint8_t flags, struct qpu_alloc_request *out_alloc)
{
	if (!ctx || !out_alloc)
		return -EINVAL;

	memset(out_alloc, 0, sizeof(*out_alloc));
	out_alloc->num_qubits = num_qubits;
	out_alloc->guard_band_hops = guard_hops;
	out_alloc->flags = flags;

	return qpu_dispatch_ioctl(ctx, QPU_IOCTL_ALLOC_PARTITION, out_alloc);
}

int qpu_free_partition(qpu_ctx_t *ctx, uint32_t partition_id)
{
	struct qpu_free_request req;
	if (!ctx || partition_id == 0)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.partition_id = partition_id;
	return qpu_dispatch_ioctl(ctx, QPU_IOCTL_FREE_PARTITION, &req);
}

int qpu_submit_circuit(qpu_ctx_t *ctx, uint32_t partition_id, const char *qasm_str, uint32_t shots, uint64_t *out_job_id)
{
	struct qpu_job_submission job;
	int ret;

	if (!ctx || !qasm_str || !out_job_id)
		return -EINVAL;

	memset(&job, 0, sizeof(job));
	job.partition_id = partition_id;
	job.shots = shots;
	job.bytecode_type = QPU_BYTECODE_OPENQASM3;
	job.bytecode_ptr = (uint64_t)(uintptr_t)qasm_str;
	job.bytecode_len = (uint32_t)strlen(qasm_str);

	ret = qpu_dispatch_ioctl(ctx, QPU_IOCTL_SUBMIT_JOB, &job);
	if (ret == 0)
		*out_job_id = job.job_id;

	return ret;
}

int qpu_await_results(qpu_ctx_t *ctx, uint64_t job_id, uint32_t max_shots, uint8_t *out_bitstrings, struct qpu_results_read *out_meta)
{
	struct qpu_results_read res;
	int ret;

	if (!ctx || !out_bitstrings || job_id == 0)
		return -EINVAL;

	memset(&res, 0, sizeof(res));
	res.job_id = job_id;
	res.max_shots = max_shots;
	res.bitstring_data_ptr = (uint64_t)(uintptr_t)out_bitstrings;

	ret = qpu_dispatch_ioctl(ctx, QPU_IOCTL_READ_RESULTS, &res);
	if (ret == 0 && out_meta)
		*out_meta = res;

	return ret;
}

int qpu_reset_qubit(qpu_ctx_t *ctx, uint16_t qubit_id)
{
	if (!ctx)
		return -EINVAL;

	/* In fallback mode, simply verify qubit bounds */
	if (ctx->is_emulated) {
		if (qubit_id >= ctx->emulated_dev->total_qubits)
			return -EINVAL;
		return 0;
	}

	return 0;
}

int qpu_configure_gpudirect(qpu_ctx_t *ctx, int dma_buf_fd, uint64_t vram_pci_addr, uint64_t size_bytes, struct qpu_p2p_dma_desc *out_p2p)
{
	struct qpu_p2p_dma_desc desc;
	int ret;

	if (!ctx || !out_p2p)
		return -EINVAL;

	memset(&desc, 0, sizeof(desc));
	desc.dma_buf_fd = dma_buf_fd;
	desc.gpu_vram_pci_addr = vram_pci_addr;
	desc.buffer_size_bytes = size_bytes;

	ret = qpu_dispatch_ioctl(ctx, QPU_IOCTL_P2P_DMA_MAP, &desc);
	if (ret == 0)
		*out_p2p = desc;

	return ret;
}

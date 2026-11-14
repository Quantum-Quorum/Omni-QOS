/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OmniQ-OS: Real-Time Quantum Linux
 *
 * Userland POSIX Client Library Header (libomniq-qpu)
 *
 * Copyright (C) 2026 OmniQ Project.
 */

#ifndef _OMNIQ_QPU_H
#define _OMNIQ_QPU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/qpu.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qpu_context qpu_ctx_t;

/**
 * Open connection to the QPU accelerator (/dev/accel/qpu0 or emulated fallback).
 * Returns NULL on critical error.
 */
qpu_ctx_t *qpu_open(const char *device_path);

/**
 * Close connection and free context.
 */
void qpu_close(qpu_ctx_t *ctx);

/**
 * Query physical QPU topology, qubit counts, and cryogenic status.
 */
int qpu_get_topology(qpu_ctx_t *ctx, struct qpu_topology_info *top);

/**
 * Query per-qubit calibration parameters (T1, T2, frequencies, fidelities).
 */
int qpu_get_calibration(qpu_ctx_t *ctx, uint16_t start_qubit, uint16_t count, struct qpu_calibration_info *cal);

/**
 * Allocate a spatially-isolated quantum partition with guard-band shielding.
 */
int qpu_allocate_partition(qpu_ctx_t *ctx, uint16_t num_qubits, uint8_t guard_hops, uint8_t flags, struct qpu_alloc_request *out_alloc);

/**
 * Release an active quantum partition and free guard bands.
 */
int qpu_free_partition(qpu_ctx_t *ctx, uint32_t partition_id);

/**
 * Submit an OpenQASM 3.0 quantum circuit for execution.
 */
int qpu_submit_circuit(qpu_ctx_t *ctx, uint32_t partition_id, const char *qasm_str, uint32_t shots, uint64_t *out_job_id);

/**
 * Await and retrieve measurement shot bitstrings.
 */
int qpu_await_results(qpu_ctx_t *ctx, uint64_t job_id, uint32_t max_shots, uint8_t *out_bitstrings, struct qpu_results_read *out_meta);

/**
 * Send active microwave reset pulse to a specific physical qubit.
 */
int qpu_reset_qubit(qpu_ctx_t *ctx, uint16_t qubit_id);

/**
 * Configure GPUDirect-QPU zero-copy DMA peer-to-peer BAR mapping.
 */
int qpu_configure_gpudirect(qpu_ctx_t *ctx, int dma_buf_fd, uint64_t vram_pci_addr, uint64_t size_bytes, struct qpu_p2p_dma_desc *out_p2p);

#ifdef __cplusplus
}
#endif

#endif /* _OMNIQ_QPU_H */

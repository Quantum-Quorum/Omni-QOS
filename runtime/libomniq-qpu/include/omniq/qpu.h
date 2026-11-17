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

qpu_ctx_t *qpu_open(const char *device_path);

void qpu_close(qpu_ctx_t *ctx);

int qpu_is_emulated(qpu_ctx_t *ctx);

int qpu_get_topology(qpu_ctx_t *ctx, struct qpu_topology_info *top);

int qpu_get_calibration(qpu_ctx_t *ctx, uint16_t start_qubit, uint16_t count, struct qpu_calibration_info *cal);

int qpu_allocate_partition(qpu_ctx_t *ctx, uint16_t num_qubits, uint8_t guard_hops, uint8_t flags, struct qpu_alloc_request *out_alloc);

int qpu_free_partition(qpu_ctx_t *ctx, uint32_t partition_id);

int qpu_submit_circuit(qpu_ctx_t *ctx, uint32_t partition_id, const char *qasm_str, uint32_t shots, uint64_t *out_job_id);

int qpu_await_results(qpu_ctx_t *ctx, uint64_t job_id, uint32_t max_shots, uint8_t *out_bitstrings, struct qpu_results_read *out_meta);

int qpu_reset_qubit(qpu_ctx_t *ctx, uint16_t qubit_id);

int qpu_configure_gpudirect(qpu_ctx_t *ctx, int dma_buf_fd, uint64_t vram_pci_addr, uint64_t size_bytes, struct qpu_p2p_dma_desc *out_p2p);

int qpu_trip_interlock(qpu_ctx_t *ctx, const char *reason, struct qpu_interlock_state *out_state);
int qpu_reset_interlock(qpu_ctx_t *ctx, struct qpu_interlock_state *out_state);
int qpu_set_cryo_temp(qpu_ctx_t *ctx, uint32_t temp_mxc_uK, struct qpu_interlock_state *out_state);
int qpu_get_interlock(qpu_ctx_t *ctx, struct qpu_interlock_state *out_state);

#ifdef __cplusplus
}
#endif

#endif

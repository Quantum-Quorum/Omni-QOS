/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OMNIQ_CORE_C_API_H
#define OMNIQ_CORE_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int omniq_core_init(void);
void omniq_core_shutdown(void);

/* Real-time engine status & benchmark JSON */
int omniq_core_get_status_json(char *out_buf, size_t max_len);
int omniq_core_run_benchmarks_json(char *out_buf, size_t max_len);

/* Pulse arena slab allocator & bounded-latency incremental GC */
uint64_t omniq_arena_alloc(size_t size_bytes);
int omniq_arena_free(uint64_t handle);
double omniq_gc_step(double budget_us);

/* Zero-copy SHM telemetry update */
int omniq_shm_publish(uint32_t mxc_temp_uK, uint32_t qec_latency_ns, uint32_t interlock_tripped, double jitter_us);

/* High-speed C++20 Quantum Statevector & Hilbert Space Simulator (1..24 qubits, OpenMP + AVX2/FMA)
 * Executes OpenQASM 2.0/3.0 or gate JSON and returns JSON with:
 * - statevector amplitudes (re, im, prob, phase)
 * - per-qubit reduced Bloch vectors (bx, by, bz, purity)
 * - bipartition Von Neumann entanglement entropy & Schmidt coefficients
 * - shot measurement counts
 */
int omniq_simulate_qasm_json(const char *qasm_str, int shots, double depol_noise, char *out_buf, size_t max_len);

/* Aaronson-Gottesman CHP Bit-Packed Clifford Stabilizer Tableau Simulator (1..1024 qubits) */
int omniq_simulate_clifford_json(const char *qasm_str, int shots, char *out_buf, size_t max_len);

/* Real Hardware RT Scheduling Wakeup Latency & Jitter Profiler (clock_nanosleep TIMER_ABSTIME) */
int omniq_measure_rt_jitter_json(int samples, int interval_us, char *out_buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* OMNIQ_CORE_C_API_H */

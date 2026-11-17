#ifndef OMNIQ_CORE_C_API_H
#define OMNIQ_CORE_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int omniq_core_init(void);
void omniq_core_shutdown(void);

int omniq_core_get_status_json(char *out_buf, size_t max_len);
int omniq_core_run_benchmarks_json(char *out_buf, size_t max_len);

uint64_t omniq_arena_alloc(size_t size_bytes);
int omniq_arena_free(uint64_t handle);
double omniq_gc_step(double budget_us);

int omniq_shm_publish(uint32_t mxc_temp_uK, uint32_t qec_latency_ns, uint32_t interlock_tripped, double jitter_us);

int omniq_simulate_qasm_json(const char *qasm_str, int shots, double depol_noise, char *out_buf, size_t max_len);

int omniq_simulate_clifford_json(const char *qasm_str, int shots, char *out_buf, size_t max_len);

int omniq_measure_rt_jitter_json(int samples, int interval_us, char *out_buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif

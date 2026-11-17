#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include "../kernel/include/uapi/linux/qpu.h"
#include "../kernel/drivers/accel/qpu/qpu_internal.h"

struct qpu_device *g_qpu_dev = NULL;

static void test_topology_query(void)
{
	struct qpu_topology_info top;
	long ret;

	printf("[TEST] 1. Querying Device Topology...\n");
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_QUERY_TOPOLOGY, (unsigned long)&top);
	assert(ret == 0);

	printf("       * Driver: %s\n", top.driver_version);
	printf("       * Device: %s\n", top.device_name);
	printf("       * State: %u (SIMULATOR=%u)\n", top.operating_state, QPU_STATE_SIMULATOR);
	printf("       * Total Physical Qubits: %u\n", top.total_qubits);
	printf("       * Total Planar Couplers: %u\n", top.total_couplers);
	printf("       * Cryo Mixing Chamber: %u.%03u mK\n",
	       top.cryo.temp_mxc_mK / 1000, top.cryo.temp_mxc_mK % 1000);
	printf("       * Global Avg T1: %llu us, Avg T2: %llu us\n",
	       (unsigned long long)(top.global_avg_t1_ns / 1000),
	       (unsigned long long)(top.global_avg_t2_ns / 1000));

	assert(top.total_qubits == 127);
	assert(top.total_couplers > 100);
	assert(top.cryo.temp_mxc_mK < top.cryo.threshold_mK);
	printf("       [PASS] Topology query succeeded.\n\n");
}

static void test_calibration_query(void)
{
	struct qpu_calibration_info cal;
	long ret;

	printf("[TEST] 2. Querying Per-Qubit Calibration Matrix...\n");
	memset(&cal, 0, sizeof(cal));
	cal.count = 8;
	cal.start_qubit_id = 0;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_QUERY_CALIBRATION, (unsigned long)&cal);
	assert(ret == 0);
	assert(cal.count == 8);

	for (int i = 0; i < 4; i++) {
		printf("       * Qubit %u: freq=%.4f GHz, T1=%u us, T2=%u us, readout_err=%.2f%%\n",
		       cal.qubits[i].qubit_id,
		       (double)cal.qubits[i].frequency_hz / 1e9,
		       cal.qubits[i].t1_ns / 1000,
		       cal.qubits[i].t2_ns / 1000,
		       (double)cal.qubits[i].readout_error_ppm / 10000.0);
		assert(cal.qubits[i].t1_ns > 100000);
		assert(cal.qubits[i].t2_ns > 80000);
	}
	printf("       [PASS] Calibration query succeeded.\n\n");
}

static void test_qmmu_spatial_multitenancy(void)
{
	struct qpu_alloc_request req_a, req_b;
	long ret;

	printf("[TEST] 3. Testing QMMU Spatial Multi-Tenancy & Crosstalk Guard Bands...\n");

	memset(&req_a, 0, sizeof(req_a));
	req_a.num_qubits = 5;
	req_a.guard_band_hops = 1;
	req_a.flags = QPU_ALLOC_FLAG_STRICT_ISOLATION;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req_a);
	assert(ret == 0);
	printf("       * Tenant A: Partition ID #%u\n", req_a.partition_id);
	printf("         Allocated %u Qubits: [", req_a.actual_allocated_count);
	for (int i = 0; i < req_a.actual_allocated_count; i++)
		printf("%u%s", req_a.allocated_qubits[i], (i + 1 == req_a.actual_allocated_count) ? "" : ", ");
	printf("]\n");

	printf("         Shield Guard Bands: %u Qubits: [", req_a.actual_guard_count);
	for (int i = 0; i < req_a.actual_guard_count; i++)
		printf("G%u%s", req_a.guard_qubits[i], (i + 1 == req_a.actual_guard_count) ? "" : ", ");
	printf("]\n");

	assert(req_a.actual_allocated_count == 5);
	assert(req_a.actual_guard_count > 0);

	memset(&req_b, 0, sizeof(req_b));
	req_b.num_qubits = 8;
	req_b.guard_band_hops = 2;
	req_b.flags = QPU_ALLOC_FLAG_STRICT_ISOLATION;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req_b);
	assert(ret == 0);
	printf("       * Tenant B: Partition ID #%u (2-hop guard count=%u)\n", req_b.partition_id, req_b.actual_guard_count);
	assert(req_b.actual_allocated_count == 8);
	assert(req_b.partition_id != req_a.partition_id);

	for (int i = 0; i < req_a.actual_allocated_count; i++) {
		for (int j = 0; j < req_b.actual_allocated_count; j++) {
			assert(req_a.allocated_qubits[i] != req_b.allocated_qubits[j]);
		}
	}
	printf("       [PASS] Spatial isolation & multi-hop crosstalk guard-band shield confirmed.\n\n");

	struct qpu_free_request free_req;
	free_req.partition_id = req_a.partition_id;
	free_req.flags = 0;
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_FREE_PARTITION, (unsigned long)&free_req);
	assert(ret == 0);

	free_req.partition_id = req_b.partition_id;
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_FREE_PARTITION, (unsigned long)&free_req);
	assert(ret == 0);
	printf("       [PASS] Partitions and guard bands freed cleanly.\n\n");
}

static void test_circuit_execution(void)
{
	struct qpu_alloc_request req;
	struct qpu_job_submission job;
	struct qpu_results_read res;
	long ret;
	uint8_t bitstrings[1024];

	printf("[TEST] 4. Submitting Quantum Circuit (Bell-State Experiment)...\n");

	memset(&req, 0, sizeof(req));
	req.num_qubits = 2;
	req.guard_band_hops = 1;
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req);
	assert(ret == 0);

	const char *bell_qasm = "OPENQASM 3.0;\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n";
	memset(&job, 0, sizeof(job));
	job.partition_id = req.partition_id;
	job.shots = 1000;
	job.bytecode_type = QPU_BYTECODE_OPENQASM3;
	job.bytecode_ptr = (uint64_t)(uintptr_t)bell_qasm;
	job.bytecode_len = (uint32_t)strlen(bell_qasm);

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SUBMIT_JOB, (unsigned long)&job);
	assert(ret == 0);
	assert(job.job_id > 0);
	printf("       * Job Submitted: Ticket #%llu (1000 shots)\n", (unsigned long long)job.job_id);

	memset(&res, 0, sizeof(res));
	memset(bitstrings, 0, sizeof(bitstrings));
	res.job_id = job.job_id;
	res.max_shots = 1000;
	res.bitstring_data_ptr = (uint64_t)(uintptr_t)bitstrings;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_READ_RESULTS, (unsigned long)&res);
	assert(ret == 0);
	assert(res.returned_shots == 1000);
	assert(res.bitstring_width == 2);

	int count_00 = 0, count_11 = 0, count_other = 0;
	for (uint32_t s = 0; s < res.returned_shots; s++) {
		uint8_t byte = bitstrings[s];
		uint8_t measurement = byte & 0x03;
		if (measurement == 0)
			count_00++;
		else if (measurement == 3)
			count_11++;
		else
			count_other++;
	}

	printf("       * Measurement Results Histogram:\n");
	printf("         |00>: %d shots (%.1f%%)\n", count_00, (double)count_00 / 10.0);
	printf("         |11>: %d shots (%.1f%%)\n", count_11, (double)count_11 / 10.0);
	printf("         Noise/Errors: %d shots (%.1f%%)\n", count_other, (double)count_other / 10.0);
	printf("       * Execution Duration: %llu ns\n", (unsigned long long)res.execution_time_ns);

	assert(count_00 > 400 && count_00 < 600);
	assert(count_11 > 400 && count_11 < 600);
	printf("       [PASS] Bell-state quantum correlations verified.\n\n");

	struct qpu_free_request free_req;
	free_req.partition_id = req.partition_id;
	qpu_dev_ioctl(NULL, QPU_IOCTL_FREE_PARTITION, (unsigned long)&free_req);
}

static void test_gpudirect_dma(void)
{
	struct qpu_p2p_dma_desc p2p;
	long ret;

	printf("[TEST] 5. Verifying GPUDirect-QPU Zero-Copy DMA Ring & Benchmark...\n");
	memset(&p2p, 0, sizeof(p2p));
	p2p.dma_buf_fd = 7;
	p2p.partition_id = 1;
	p2p.buffer_size_bytes = 4096;
	p2p.gpu_vram_pci_addr = 0x0000380000000000ULL;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_P2P_DMA_MAP, (unsigned long)&p2p);
	assert(ret == 0);
	assert(p2p.p2p_status == 0);
	assert(g_qpu_dev->dma_ring_virt != NULL);
	assert(g_qpu_dev->dma_ring_size == QPU_DMA_RING_SIZE);
	assert(p2p.latency_budget_ns > 0 && p2p.latency_budget_ns < 5000000);

	printf("       * Zero-Copy DMA Ring: %zu KB allocated at %p\n",
	       g_qpu_dev->dma_ring_size / 1024, g_qpu_dev->dma_ring_virt);
	printf("       * P2P Status: RING_ACTIVE (0)\n");
	printf("       * Measured Zero-Copy Syndrome Ring Latency: %u ns\n", p2p.latency_budget_ns);
	printf("       [PASS] GPUDirect-QPU zero-copy DMA ring verified.\n\n");
}

static void test_cryo_thermal_interlock(void)
{
	struct qpu_interlock_state st;
	struct qpu_alloc_request req;
	struct qpu_job_submission job;
	long ret;

	printf("[TEST] 6. Verifying < 50.000 mK Cryo Thermal Interlock & RF DAC Clamp...\n");

	memset(&req, 0, sizeof(req));
	req.num_qubits = 2;
	req.guard_band_hops = 1;
	assert(qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req) == 0);

	memset(&st, 0, sizeof(st));
	st.temp_mxc_mK = 65000;
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SET_CRYO_TEMP, (unsigned long)&st);
	assert(ret == 0);
	assert(st.tripped == 1);
	assert(st.rf_dac_clamped == 1);

	const char *qasm = "OPENQASM 3.0;\nqubit[2] q;\nh q[0];\n";
	memset(&job, 0, sizeof(job));
	job.partition_id = req.partition_id;
	job.shots = 100;
	job.bytecode_type = QPU_BYTECODE_OPENQASM3;
	job.bytecode_ptr = (uint64_t)(uintptr_t)qasm;
	job.bytecode_len = (uint32_t)strlen(qasm);
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SUBMIT_JOB, (unsigned long)&job);
	assert(ret == -13);

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_RESET_INTERLOCK, (unsigned long)&st);
	assert(ret == -16);

	st.temp_mxc_mK = 14250;
	assert(qpu_dev_ioctl(NULL, QPU_IOCTL_SET_CRYO_TEMP, (unsigned long)&st) == 0);
	assert(qpu_dev_ioctl(NULL, QPU_IOCTL_RESET_INTERLOCK, (unsigned long)&st) == 0);
	assert(st.tripped == 0 && st.rf_dac_clamped == 0);

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SUBMIT_JOB, (unsigned long)&job);
	assert(ret == 0);

	struct qpu_free_request free_req = { .partition_id = req.partition_id, .flags = 0 };
	qpu_dev_ioctl(NULL, QPU_IOCTL_FREE_PARTITION, (unsigned long)&free_req);
	printf("       [PASS] Cryo Thermal Interlock auto-trip, RF 0x00 clamp, and cool-down reset verified.\n\n");
}

static void test_fixed_point_cordic_statevector(void)
{
	struct qpu_alloc_request req;
	struct qpu_job_submission job;
	struct qpu_results_read res;
	struct qpu_free_request free_req;
	uint8_t bitstrings[2048];
	long ret;

	printf("[TEST] 7. Verifying Kernel Fixed-Point + CORDIC OpenQASM Statevector Execution...\n");

	memset(&req, 0, sizeof(req));
	req.num_qubits = 3;
	req.guard_band_hops = 1;
	assert(qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req) == 0);

	{
		const char *qasm_a = "OPENQASM 3.0; qubit[2] q; x q[1];";
		int count_10 = 0;

		memset(&job, 0, sizeof(job));
		job.partition_id = req.partition_id;
		job.shots = 500;
		job.bytecode_type = QPU_BYTECODE_OPENQASM3;
		job.bytecode_ptr = (uint64_t)(uintptr_t)qasm_a;
		job.bytecode_len = (uint32_t)strlen(qasm_a);

		ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SUBMIT_JOB, (unsigned long)&job);
		assert(ret == 0);

		memset(&res, 0, sizeof(res));
		memset(bitstrings, 0, sizeof(bitstrings));
		res.job_id = job.job_id;
		res.max_shots = 500;
		res.bitstring_data_ptr = (uint64_t)(uintptr_t)bitstrings;

		ret = qpu_dev_ioctl(NULL, QPU_IOCTL_READ_RESULTS, (unsigned long)&res);
		assert(ret == 0);
		assert(res.returned_shots == 500);
		assert(res.bitstring_width == 2);
		assert(res.execution_time_ns > 0);

		for (uint32_t s = 0; s < res.returned_shots; s++) {
			if ((bitstrings[s] & 0x03) == 0x02)
				count_10++;
		}
		printf("       * [7A] Pauli-X q[1] (500 shots): |10> = %d/500 (exec=%llu ns)\n",
		       count_10, (unsigned long long)res.execution_time_ns);
		assert(count_10 > 470);
	}

	{
		const char *qasm_b = "OPENQASM 3.0; qubit[3] q; h q[0]; h q[1]; ccx q[0], q[1], q[2];";
		int hist[8] = { 0 };

		memset(&job, 0, sizeof(job));
		job.partition_id = req.partition_id;
		job.shots = 1000;
		job.bytecode_type = QPU_BYTECODE_OPENQASM3;
		job.bytecode_ptr = (uint64_t)(uintptr_t)qasm_b;
		job.bytecode_len = (uint32_t)strlen(qasm_b);

		ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SUBMIT_JOB, (unsigned long)&job);
		assert(ret == 0);

		memset(&res, 0, sizeof(res));
		memset(bitstrings, 0, sizeof(bitstrings));
		res.job_id = job.job_id;
		res.max_shots = 1000;
		res.bitstring_data_ptr = (uint64_t)(uintptr_t)bitstrings;

		ret = qpu_dev_ioctl(NULL, QPU_IOCTL_READ_RESULTS, (unsigned long)&res);
		assert(ret == 0);
		assert(res.returned_shots == 1000);
		assert(res.bitstring_width == 3);
		assert(res.execution_time_ns > 0);

		for (uint32_t s = 0; s < res.returned_shots; s++) {
			hist[bitstrings[s] & 0x07]++;
		}
		printf("       * [7B] Toffoli CCX (1000 shots): |000>=%d, |001>=%d, |010>=%d, |111>=%d, |011>=%d (exec=%llu ns)\n",
		       hist[0], hist[1], hist[2], hist[7], hist[3],
		       (unsigned long long)res.execution_time_ns);

		assert(hist[0] >= 180 && hist[0] <= 320);
		assert(hist[1] >= 180 && hist[1] <= 320);
		assert(hist[2] >= 180 && hist[2] <= 320);
		assert(hist[7] >= 180 && hist[7] <= 320);
		assert(hist[3] < 30);
	}

	{
		const char *qasm_c = "OPENQASM 3.0; qubit[1] q; ry(pi/2) q[0];";
		int count_0 = 0, count_1 = 0;

		memset(&job, 0, sizeof(job));
		job.partition_id = req.partition_id;
		job.shots = 1000;
		job.bytecode_type = QPU_BYTECODE_OPENQASM3;
		job.bytecode_ptr = (uint64_t)(uintptr_t)qasm_c;
		job.bytecode_len = (uint32_t)strlen(qasm_c);

		ret = qpu_dev_ioctl(NULL, QPU_IOCTL_SUBMIT_JOB, (unsigned long)&job);
		assert(ret == 0);

		memset(&res, 0, sizeof(res));
		memset(bitstrings, 0, sizeof(bitstrings));
		res.job_id = job.job_id;
		res.max_shots = 1000;
		res.bitstring_data_ptr = (uint64_t)(uintptr_t)bitstrings;

		ret = qpu_dev_ioctl(NULL, QPU_IOCTL_READ_RESULTS, (unsigned long)&res);
		assert(ret == 0);
		assert(res.returned_shots == 1000);
		assert(res.bitstring_width == 1);
		assert(res.execution_time_ns > 0);

		for (uint32_t s = 0; s < res.returned_shots; s++) {
			if ((bitstrings[s] & 0x01) == 0)
				count_0++;
			else
				count_1++;
		}
		printf("       * [7C] CORDIC ry(pi/2) (1000 shots): |0>=%d, |1>=%d (exec=%llu ns)\n",
		       count_0, count_1, (unsigned long long)res.execution_time_ns);
		assert(count_0 >= 420 && count_0 <= 580);
		assert(count_1 >= 420 && count_1 <= 580);
	}

	free_req.partition_id = req.partition_id;
	free_req.flags = 0;
	assert(qpu_dev_ioctl(NULL, QPU_IOCTL_FREE_PARTITION, (unsigned long)&free_req) == 0);
	printf("       [PASS] Q2.30 Fixed-Point + 24-Step CORDIC Statevector Execution verified.\n\n");
}

int main(void)
{
	printf("=================================================================\n");
	printf(" OmniQ-OS: Kernel UAPI & Fallback Driver Validation Suite        \n");
	printf("=================================================================\n\n");

	g_qpu_dev = (struct qpu_device *)calloc(1, sizeof(struct qpu_device));
	assert(g_qpu_dev != NULL);
	qpu_fallback_init(g_qpu_dev);

	test_topology_query();
	test_calibration_query();
	test_qmmu_spatial_multitenancy();
	test_circuit_execution();
	test_gpudirect_dma();
	test_cryo_thermal_interlock();
	test_fixed_point_cordic_statevector();

	qpu_fallback_cleanup(g_qpu_dev);
	free(g_qpu_dev);
	printf("=================================================================\n");
	printf(" ALL OMNIQ-OS KERNEL UAPI TESTS PASSED SUCCESSFULLY!             \n");
	printf("=================================================================\n");
	return 0;
}

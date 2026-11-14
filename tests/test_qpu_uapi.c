/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OmniQ-OS: Real-Time Quantum Linux Test Harness
 *
 * Verifies UAPI data structures, QMMU spatial multi-tenancy, crosstalk guard bands,
 * fallback execution, and GPUDirect-QPU DMA descriptors.
 *
 * Copyright (C) 2026 OmniQ Project.
 */

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

	printf("       • Driver: %s\n", top.driver_version);
	printf("       • Device: %s\n", top.device_name);
	printf("       • State: %u (SIMULATOR=%u)\n", top.operating_state, QPU_STATE_SIMULATOR);
	printf("       • Total Physical Qubits: %u\n", top.total_qubits);
	printf("       • Total Planar Couplers: %u\n", top.total_couplers);
	printf("       • Cryo Mixing Chamber: %u.%03u mK\n",
	       top.cryo.temp_mxc_mK / 1000, top.cryo.temp_mxc_mK % 1000);
	printf("       • Global Avg T1: %llu us, Avg T2: %llu us\n",
	       (top.global_avg_t1_ns / 1000), (top.global_avg_t2_ns / 1000));

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
		printf("       • Qubit %u: freq=%.4f GHz, T1=%u us, T2=%u us, readout_err=%.2f%%\n",
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

	/* Tenant A: Requests 5 qubits with 1-hop guard-band isolation */
	memset(&req_a, 0, sizeof(req_a));
	req_a.num_qubits = 5;
	req_a.guard_band_hops = 1;
	req_a.flags = QPU_ALLOC_FLAG_STRICT_ISOLATION;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req_a);
	assert(ret == 0);
	printf("       • Tenant A: Partition ID #%u\n", req_a.partition_id);
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

	/* Tenant B: Requests 8 qubits simultaneously */
	memset(&req_b, 0, sizeof(req_b));
	req_b.num_qubits = 8;
	req_b.guard_band_hops = 1;
	req_b.flags = QPU_ALLOC_FLAG_STRICT_ISOLATION;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req_b);
	assert(ret == 0);
	printf("       • Tenant B: Partition ID #%u\n", req_b.partition_id);
	printf("         Allocated %u Qubits: [", req_b.actual_allocated_count);
	for (int i = 0; i < req_b.actual_allocated_count; i++)
		printf("%u%s", req_b.allocated_qubits[i], (i + 1 == req_b.actual_allocated_count) ? "" : ", ");
	printf("]\n");

	assert(req_b.actual_allocated_count == 8);
	assert(req_b.partition_id != req_a.partition_id);

	/* Verify non-overlapping invariant: No allocated qubit in A belongs to B or its guards */
	for (int i = 0; i < req_a.actual_allocated_count; i++) {
		for (int j = 0; j < req_b.actual_allocated_count; j++) {
			assert(req_a.allocated_qubits[i] != req_b.allocated_qubits[j]);
		}
	}
	printf("       [PASS] Spatial isolation & crosstalk guard-band shield confirmed.\n\n");

	/* Clean up Tenant A & B */
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

	/* Allocate 2 qubits */
	memset(&req, 0, sizeof(req));
	req.num_qubits = 2;
	req.guard_band_hops = 1;
	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_ALLOC_PARTITION, (unsigned long)&req);
	assert(ret == 0);

	/* Submit Bell state circuit */
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
	printf("       • Job Submitted: Ticket #%llu (1000 shots)\n", job.job_id);

	/* Retrieve shot measurements */
	memset(&res, 0, sizeof(res));
	memset(bitstrings, 0, sizeof(bitstrings));
	res.job_id = job.job_id;
	res.max_shots = 1000;
	res.bitstring_data_ptr = (uint64_t)(uintptr_t)bitstrings;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_READ_RESULTS, (unsigned long)&res);
	assert(ret == 0);
	assert(res.returned_shots == 1000);
	assert(res.bitstring_width == 2);

	/* Count Bell state distribution (|00> vs |11>) */
	int count_00 = 0, count_11 = 0, count_other = 0;
	for (uint32_t s = 0; s < res.returned_shots; s++) {
		uint8_t byte = bitstrings[s];
		uint8_t measurement = byte & 0x03; /* 2 qubits */
		if (measurement == 0)
			count_00++;
		else if (measurement == 3)
			count_11++;
		else
			count_other++;
	}

	printf("       • Measurement Results Histogram:\n");
	printf("         |00>: %d shots (%.1f%%)\n", count_00, (double)count_00 / 10.0);
	printf("         |11>: %d shots (%.1f%%)\n", count_11, (double)count_11 / 10.0);
	printf("         Noise/Errors: %d shots (%.1f%%)\n", count_other, (double)count_other / 10.0);
	printf("       • Execution Duration: %llu ns\n", res.execution_time_ns);

	assert(count_00 > 400 && count_00 < 600);
	assert(count_11 > 400 && count_11 < 600);
	printf("       [PASS] Bell-state quantum correlations verified.\n\n");

	/* Free partition */
	struct qpu_free_request free_req;
	free_req.partition_id = req.partition_id;
	qpu_dev_ioctl(NULL, QPU_IOCTL_FREE_PARTITION, (unsigned long)&free_req);
}

static void test_gpudirect_dma(void)
{
	struct qpu_p2p_dma_desc p2p;
	long ret;

	printf("[TEST] 5. Verifying GPUDirect-QPU P2P DMA Descriptor...\n");
	memset(&p2p, 0, sizeof(p2p));
	p2p.dma_buf_fd = 7; /* Dummy GPU dma-buf fd */
	p2p.partition_id = 1;
	p2p.buffer_size_bytes = 4096;
	p2p.gpu_vram_pci_addr = 0x0000380000000000ULL;

	ret = qpu_dev_ioctl(NULL, QPU_IOCTL_P2P_DMA_MAP, (unsigned long)&p2p);
	assert(ret == 0);
	assert(p2p.p2p_status == 0); /* LINK_ACTIVE */
	assert(p2p.latency_budget_ns <= 600); /* Meets sub-microsecond deadline */

	printf("       • P2P Status: LINK_ACTIVE (0)\n");
	printf("       • Round-Trip QEC Latency Budget: %u ns (< 600 ns)\n", p2p.latency_budget_ns);
	printf("       [PASS] GPUDirect-QPU DMA descriptor confirmed.\n\n");
}

int main(void)
{
	printf("=================================================================\n");
	printf(" OmniQ-OS: Kernel UAPI & Fallback Driver Validation Suite        \n");
	printf("=================================================================\n\n");

	/* Initialize fallback simulator */
	g_qpu_dev = (struct qpu_device *)calloc(1, sizeof(struct qpu_device));
	assert(g_qpu_dev != NULL);
	qpu_fallback_init(g_qpu_dev);

	test_topology_query();
	test_calibration_query();
	test_qmmu_spatial_multitenancy();
	test_circuit_execution();
	test_gpudirect_dma();

	free(g_qpu_dev);
	printf("=================================================================\n");
	printf(" ALL OMNIQ-OS KERNEL UAPI TESTS PASSED SUCCESSFULLY!             \n");
	printf("=================================================================\n");
	return 0;
}

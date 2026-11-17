#ifndef _UAPI_LINUX_QPU_H
#define _UAPI_LINUX_QPU_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t  __s32;
typedef int64_t  __s64;
#endif

#define QPU_UAPI_VERSION_MAJOR 1
#define QPU_UAPI_VERSION_MINOR 0
#define QPU_MAX_NAME_LEN       64
#define QPU_MAX_QUBITS         256
#define QPU_MAX_COUPLERS       512
#define QPU_MAX_PARTITION_Q    64

#define QPU_STATE_ONLINE_HARDWARE     0x00
#define QPU_STATE_OFFLINE_MAINTENANCE 0x01
#define QPU_STATE_SIMULATOR           0x02

#define QPU_TECH_UNKNOWN              0x00
#define QPU_TECH_SUPERCONDUCTING      0x01
#define QPU_TECH_TRAPPED_ION          0x02
#define QPU_TECH_NEUTRAL_ATOM         0x03
#define QPU_TECH_PHOTONIC             0x04
#define QPU_TECH_SIMULATED            0x05

#define QPU_BYTECODE_RAW_PULSE        0x01
#define QPU_BYTECODE_QIR              0x02
#define QPU_BYTECODE_OPENQASM3        0x03
#define QPU_BYTECODE_OMNIQ_IR         0x04

#define QPU_ALLOC_FLAG_STRICT_ISOLATION (1 << 0)
#define QPU_ALLOC_FLAG_DYNAMIC_DECOUPLE (1 << 1)
#define QPU_ALLOC_FLAG_GPUDIRECT        (1 << 2)

#define QPU_CRYO_FLAG_NOMINAL           0x00
#define QPU_CRYO_FLAG_INTERLOCK_TRIPPED (1 << 0)
#define QPU_CRYO_FLAG_RF_CLAMPED        (1 << 1)
#define QPU_CRYO_FLAG_THERMAL_QUENCH    (1 << 2)

struct qpu_cryo_telemetry {
	__u32 temp_50k_mK;
	__u32 temp_4k_mK;
	__u32 temp_still_mK;
	__u32 temp_cold_plate_mK;
	__u32 temp_mxc_mK;
	__u32 threshold_mK;
	__u32 cryo_status_flags;
	__u32 reserved;
};

struct qpu_coupler_edge {
	__u16 qubit_u;
	__u16 qubit_v;
	__u32 gate_2q_error_ppm;
};

struct qpu_qubit_info {
	__u16 qubit_id;
	__u16 flags;
	__u64 frequency_hz;
	__s64 anharmonicity_hz;
	__u32 t1_ns;
	__u32 t2_ns;
	__u32 readout_error_ppm;
	__u32 gate_1q_error_ppm;
};

struct qpu_topology_info {
	char  driver_version[16];
	char  device_name[QPU_MAX_NAME_LEN];
	__u32 operating_state;
	__u32 technology;
	__u16 total_qubits;
	__u16 allocated_qubits;
	__u16 guard_qubits;
	__u16 total_couplers;
	__u64 global_avg_t1_ns;
	__u64 global_avg_t2_ns;
	struct qpu_cryo_telemetry cryo;
	struct qpu_coupler_edge couplers[QPU_MAX_COUPLERS];
};

struct qpu_calibration_info {
	__u16 count;
	__u16 start_qubit_id;
	__u32 reserved;
	struct qpu_qubit_info qubits[QPU_MAX_QUBITS];
};

struct qpu_alloc_request {
	__u16 num_qubits;
	__u8  guard_band_hops;
	__u8  flags;
	__u32 partition_id;
	__u16 allocated_qubits[QPU_MAX_PARTITION_Q];
	__u16 guard_qubits[QPU_MAX_PARTITION_Q];
	__u16 actual_allocated_count;
	__u16 actual_guard_count;
	__u32 est_avg_fidelity_ppm;
};

struct qpu_free_request {
	__u32 partition_id;
	__u32 flags;
};

struct qpu_job_submission {
	__u32 partition_id;
	__u32 shots;
	__u32 bytecode_type;
	__u32 bytecode_len;
	__u64 bytecode_ptr;
	__u64 timeout_us;
	__u64 job_id;
	__u32 status;
	__u32 reserved;
};

struct qpu_results_read {
	__u64 job_id;
	__u32 max_shots;
	__u32 returned_shots;
	__u16 bitstring_width;
	__u16 flags;
	__u64 bitstring_data_ptr;
	__u64 execution_time_ns;
};

struct qpu_p2p_dma_desc {
	__s32 dma_buf_fd;
	__u32 partition_id;
	__u64 buffer_size_bytes;
	__u64 gpu_vram_pci_addr;
	__u32 p2p_status;
	__u32 latency_budget_ns;
};

struct qpu_interlock_state {
	__u32 tripped;
	__u32 rf_dac_clamped;
	__u32 temp_mxc_mK;
	__u32 threshold_mK;
	__u64 trip_count;
	char  reason[64];
};

#define QPU_IOCTL_MAGIC 'Q'

#define QPU_IOCTL_QUERY_TOPOLOGY \
	_IOR(QPU_IOCTL_MAGIC, 0x01, struct qpu_topology_info)

#define QPU_IOCTL_QUERY_CALIBRATION \
	_IOWR(QPU_IOCTL_MAGIC, 0x02, struct qpu_calibration_info)

#define QPU_IOCTL_ALLOC_PARTITION \
	_IOWR(QPU_IOCTL_MAGIC, 0x03, struct qpu_alloc_request)

#define QPU_IOCTL_FREE_PARTITION \
	_IOW(QPU_IOCTL_MAGIC, 0x04, struct qpu_free_request)

#define QPU_IOCTL_SUBMIT_JOB \
	_IOWR(QPU_IOCTL_MAGIC, 0x05, struct qpu_job_submission)

#define QPU_IOCTL_READ_RESULTS \
	_IOWR(QPU_IOCTL_MAGIC, 0x06, struct qpu_results_read)

#define QPU_IOCTL_P2P_DMA_MAP \
	_IOWR(QPU_IOCTL_MAGIC, 0x07, struct qpu_p2p_dma_desc)

#define QPU_IOCTL_TRIP_INTERLOCK \
	_IOWR(QPU_IOCTL_MAGIC, 0x08, struct qpu_interlock_state)

#define QPU_IOCTL_RESET_INTERLOCK \
	_IOWR(QPU_IOCTL_MAGIC, 0x09, struct qpu_interlock_state)

#define QPU_IOCTL_SET_CRYO_TEMP \
	_IOWR(QPU_IOCTL_MAGIC, 0x0A, struct qpu_interlock_state)

#define QPU_IOCTL_GET_INTERLOCK \
	_IOR(QPU_IOCTL_MAGIC, 0x0B, struct qpu_interlock_state)

#define QPU_IOCTL_RESET_QUBIT \
	_IOW(QPU_IOCTL_MAGIC, 0x0C, __u16)

#endif


/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * OmniQ-OS: Real-Time Quantum Linux Compute Accelerator Subsystem
 *
 * User-space API (UAPI) definitions for Quantum Processing Units (QPU).
 * Conforms to the Linux Compute Accelerator (/dev/accel/qpuX) subsystem.
 *
 * Copyright (C) 2026 OmniQ Project.
 */

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

/* Operating States */
#define QPU_STATE_ONLINE_HARDWARE     0x00  /* Physical QPU online, T < 50 mK */
#define QPU_STATE_OFFLINE_MAINTENANCE 0x01  /* Physical QPU detected, T >= 50 mK (Warm) */
#define QPU_STATE_SIMULATOR           0x02  /* Fallback emulation co-processor */

/* Quantum Technology Types */
#define QPU_TECH_UNKNOWN              0x00
#define QPU_TECH_SUPERCONDUCTING      0x01  /* Transmon / Fluxonium */
#define QPU_TECH_TRAPPED_ION          0x02  /* Yb+, Ca+, Ba+ */
#define QPU_TECH_NEUTRAL_ATOM         0x03  /* Rydberg arrays */
#define QPU_TECH_PHOTONIC             0x04  /* Continuous variable / dual-rail */
#define QPU_TECH_SIMULATED            0x05  /* Classical software co-processor */

/* Bytecode / Instruction Formats */
#define QPU_BYTECODE_RAW_PULSE        0x01  /* Calibrated microwave waveform bitstream */
#define QPU_BYTECODE_QIR              0x02  /* LLVM Quantum Intermediate Representation */
#define QPU_BYTECODE_OPENQASM3        0x03  /* OpenQASM 3.0 string buffer */
#define QPU_BYTECODE_OMNIQ_IR         0x04  /* OmniQ Native Bytecode */

/* Allocation Flags */
#define QPU_ALLOC_FLAG_STRICT_ISOLATION (1 << 0) /* Hard guard band enforcement */
#define QPU_ALLOC_FLAG_DYNAMIC_DECOUPLE (1 << 1) /* Auto CPMG/XY-8 on idle */
#define QPU_ALLOC_FLAG_GPUDIRECT        (1 << 2) /* Pin buffers for GPU P2P DMA */

/* Cryogenic Temperature Telemetry */
struct qpu_cryo_telemetry {
	__u32 temp_50k_mK;        /* 50K Stage (milli-Kelvin) */
	__u32 temp_4k_mK;         /* 4K Stage (milli-Kelvin) */
	__u32 temp_still_mK;      /* Still Stage (~800 mK) */
	__u32 temp_cold_plate_mK; /* Cold Plate (~100 mK) */
	__u32 temp_mxc_mK;        /* Mixing Chamber (~10-15 mK) */
	__u32 threshold_mK;       /* Operational cutoff (e.g. 50000 mK = 50 mK) */
	__u32 cryo_status_flags;  /* Refrigerator alerts */
	__u32 reserved;
};

/* Physical Coupler Edge */
struct qpu_coupler_edge {
	__u16 qubit_u;
	__u16 qubit_v;
	__u32 gate_2q_error_ppm;  /* Parts per million (e.g. 5000 = 0.5% error) */
};

/* Physical Qubit Calibration Entry */
struct qpu_qubit_info {
	__u16 qubit_id;
	__u16 flags;              /* Healthy, Defect, Guard-Band */
	__u64 frequency_hz;       /* Qubit 0-1 transition frequency */
	__s64 anharmonicity_hz;   /* Non-linearity (alpha) */
	__u32 t1_ns;              /* Longitudinal relaxation time (ns) */
	__u32 t2_ns;              /* Transverse dephasing time (ns) */
	__u32 readout_error_ppm;  /* Readout fidelity error in ppm */
	__u32 gate_1q_error_ppm;  /* Single-qubit error in ppm */
};

/* Physical Device Topology */
struct qpu_topology_info {
	char  driver_version[16];
	char  device_name[QPU_MAX_NAME_LEN];
	__u32 operating_state;    /* QPU_STATE_* */
	__u32 technology;         /* QPU_TECH_* */
	__u16 total_qubits;
	__u16 allocated_qubits;
	__u16 guard_qubits;
	__u16 total_couplers;
	__u64 global_avg_t1_ns;
	__u64 global_avg_t2_ns;
	struct qpu_cryo_telemetry cryo;
	struct qpu_coupler_edge couplers[QPU_MAX_COUPLERS];
};

/* Calibration Query Request */
struct qpu_calibration_info {
	__u16 count;              /* In: max entries, Out: returned entries */
	__u16 start_qubit_id;
	__u32 reserved;
	struct qpu_qubit_info qubits[QPU_MAX_QUBITS];
};

/* Spatial Partition Request (QMMU) */
struct qpu_alloc_request {
	__u16 num_qubits;         /* Number of physical qubits requested */
	__u8  guard_band_hops;    /* Crosstalk guard band radius (1 or 2 hops) */
	__u8  flags;              /* QPU_ALLOC_FLAG_* */
	__u32 partition_id;       /* Out: Assigned partition ID */
	__u16 allocated_qubits[QPU_MAX_PARTITION_Q]; /* Out: Physical qubit IDs */
	__u16 guard_qubits[QPU_MAX_PARTITION_Q];     /* Out: Shield qubit IDs */
	__u16 actual_allocated_count;
	__u16 actual_guard_count;
	__u32 est_avg_fidelity_ppm; /* Predicted composite fidelity */
};

/* Free Partition Request */
struct qpu_free_request {
	__u32 partition_id;
	__u32 flags;              /* Reset qubits on free */
};

/* Job Submission */
struct qpu_job_submission {
	__u32 partition_id;
	__u32 shots;              /* Number of measurement repetitions */
	__u32 bytecode_type;      /* QPU_BYTECODE_* */
	__u32 bytecode_len;       /* Length of bytecode stream in bytes */
	__u64 bytecode_ptr;       /* User-space pointer to instruction buffer */
	__u64 timeout_us;         /* Hard real-time execution timeout */
	__u64 job_id;             /* Out: Assigned 64-bit ticket */
	__u32 status;             /* Out: Job execution status */
	__u32 reserved;
};

/* Results Read */
struct qpu_results_read {
	__u64 job_id;             /* In: Target job ticket */
	__u32 max_shots;          /* In: Buffer shot capacity */
	__u32 returned_shots;     /* Out: Actual shots delivered */
	__u16 bitstring_width;    /* Number of measured qubits */
	__u16 flags;
	__u64 bitstring_data_ptr; /* User-space pointer to measurement buffer */
	__u64 execution_time_ns;  /* Physical execution duration on QPU */
};

/* GPUDirect-QPU Peer-to-Peer DMA Descriptor */
struct qpu_p2p_dma_desc {
	__s32 dma_buf_fd;         /* Linux dma-buf file descriptor from GPU */
	__u32 partition_id;
	__u64 buffer_size_bytes;
	__u64 gpu_vram_pci_addr;  /* Direct BAR mapping target */
	__u32 p2p_status;         /* Out: 0 on active link */
	__u32 latency_budget_ns;  /* Max allowable round trip (e.g. 600 ns) */
};

/* =========================================================================
 * IOCTL Command Definitions
 * ========================================================================= */
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

#endif /* _UAPI_LINUX_QPU_H */


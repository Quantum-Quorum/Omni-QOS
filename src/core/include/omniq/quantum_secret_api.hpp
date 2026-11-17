/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OMNIQ_QUANTUM_SECRET_API_HPP
#define OMNIQ_QUANTUM_SECRET_API_HPP

#include <stdint.h>
#include <stddef.h>

#define OMNIQ_SECRET_MAGIC 0x4F4D4E4951534543ULL /* "OMNIQSEC" */
#define OMNIQ_SHM_MULTIMODAL_NAME "/omniq_quantum_multimodal_v1"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct OmniqMultiModalFrame {
    uint64_t magic;                      /* 8B: 0x4F4D4E4951534543ULL */
    uint32_t version;                    /* 4B: 1 */
    uint32_t frame_size_bytes;           /* 4B: 3744 */
    uint64_t sequence_id;                /* 8B */
    uint64_t timestamp_ns;               /* 8B */

    /* Modality 1: 5-Stage Cryogenic Dilution Refrigerator (64B) */
    struct {
        double stage_50k_K;
        double stage_4k_K;
        double still_mK;
        double cold_plate_mK;
        double mxc_mK;
        double he3_flow_umol_s;
        double vacuum_mbar;
        uint32_t thermal_interlock_tripped;
        uint32_t rf_dac_clamped;
    } cryo;                              /* 64B */

    /* Modality 2: 16-ch RFSoC Microwave Pulse Synthesis (544B) */
    struct {
        double lo_freq_ghz[16];
        double iq_amplitude_v[32];       /* I/Q interleaved */
        double drag_beta[16];
        double phase_noise_dbc_hz;
        double pulse_jitter_fs;
        uint64_t total_pulses_emitted;
        uint32_t dac_clamp_register;     /* 0x00 when clamped, 0x01 active */
        uint32_t active_channels;
    } rfsoc;                             /* 128 + 256 + 128 + 8 + 8 + 8 + 4 + 4 = 544B */

    /* Modality 3: Dispersive Readout & Heterodyne IQ Demodulation (536B) */
    struct {
        double resonator_freq_ghz[16];
        double iq_cloud_centers[32];
        double snr_db[16];
        double assignment_fidelity;
        double active_reset_ns;
        uint64_t total_readouts;
    } readout;                           /* 128 + 256 + 128 + 8 + 8 + 8 = 536B */

    /* Modality 4: QMMU 127-Qubit Spatial Lattice & Crosstalk (560B) */
    struct {
        uint8_t qubit_alloc_map[128];    /* 0=free, 1=alloc, 2=guard, 3=defect */
        double bank_t1_us[16];
        double bank_t2_us[16];
        double guard_isolation_db[16];
        double zz_crosstalk_khz[4];
        uint32_t active_partitions;
        uint32_t allocated_qubits;
        uint32_t guard_qubits;
        uint32_t free_qubits;
    } qmmu;                              /* 128 + 128 + 128 + 128 + 32 + 16 = 560B */

    /* Modality 5: GPUDirect-QPU Real-Time QEC Syndrome Loop (296B) */
    struct {
        uint8_t surface_code_syndromes[64];
        double decoder_stage_ns[8];
        double round_trip_latency_ns;
        double pcie_p2p_gbps;
        double logical_error_rate;
        uint32_t code_distance;
        uint32_t p2p_link_active;
        uint64_t corrected_pauli_frames;
        uint8_t reserved_qec[128];
    } qec;                               /* 64 + 64 + 24 + 8 + 8 + 128 = 296B */

    /* Modality 6: Decoherence & Two-Level-System (TLS) Drift (304B) */
    struct {
        double flux_noise_psd[16];
        double freq_drift_khz[16];
        double cpmg_coherence_gain;
        double tls_defect_density;
        uint64_t xy8_keepalive_pulses;
        uint8_t reserved_tls[24];
    } coherence;                         /* 128 + 128 + 24 + 24 = 304B */

    /* Modality 7: Tri-Compute Node & Post-Quantum Crypto Telemetry (320B) */
    struct {
        double cpu_isolated_load_pct[4];
        double gpu_tensor_util_pct;
        double gpu_hbm_used_gb;
        double preempt_rt_jitter_us;
        double tri_synergy_score;
        char pqc_kem_alg[32];            /* "ML-KEM-768 (FIPS 203)" */
        char pqc_sig_alg[32];            /* "ML-DSA-65 (FIPS 204)" */
        uint64_t pqc_key_epoch;
        uint8_t pqc_public_fingerprint[64];
        uint8_t reserved_tri[120];
    } tricompute;                        /* 32 + 32 + 64 + 8 + 64 + 120 = 320B */

    /* Padding + Tamper-Evident Frame Integrity Digest */
    uint8_t reserved_pad[1023];
    char frame_sha256[65];               /* Null-terminated hex SHA-256 */
} OmniqMultiModalFrame;
#pragma pack(pop)

/* Compile-time guarantee: exact 3,744-byte ABI frame */
#ifdef __cplusplus
static_assert(sizeof(OmniqMultiModalFrame) == 3744, "OmniqMultiModalFrame must be exactly 3744 bytes");
#endif

int omniq_secret_get_key_path(char *out_path, size_t max_len);
int omniq_secret_ensure_key(char *out_key, size_t max_len);
int omniq_secret_get_json(const char *provided_key, const char *modality, char *out_buf, size_t max_len);
int omniq_secret_get_binary_frame(const char *provided_key, OmniqMultiModalFrame *out_frame);
int omniq_secret_verify_audit_chain(int *out_entries);

#ifdef __cplusplus
}
#endif

#endif /* OMNIQ_QUANTUM_SECRET_API_HPP */

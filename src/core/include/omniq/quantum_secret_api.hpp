#ifndef OMNIQ_QUANTUM_SECRET_API_HPP
#define OMNIQ_QUANTUM_SECRET_API_HPP

#include <stdint.h>
#include <stddef.h>

#define OMNIQ_SECRET_MAGIC 0x4F4D4E4951534543ULL
#define OMNIQ_SHM_MULTIMODAL_NAME "/omniq_quantum_multimodal_v1"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct OmniqMultiModalFrame {
    uint64_t magic;
    uint32_t version;
    uint32_t frame_size_bytes;
    uint64_t sequence_id;
    uint64_t timestamp_ns;

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
    } cryo;

    struct {
        double lo_freq_ghz[16];
        double iq_amplitude_v[32];
        double drag_beta[16];
        double phase_noise_dbc_hz;
        double pulse_jitter_fs;
        uint64_t total_pulses_emitted;
        uint32_t dac_clamp_register;
        uint32_t active_channels;
    } rfsoc;

    struct {
        double resonator_freq_ghz[16];
        double iq_cloud_centers[32];
        double snr_db[16];
        double assignment_fidelity;
        double active_reset_ns;
        uint64_t total_readouts;
    } readout;

    struct {
        uint8_t qubit_alloc_map[128];
        double bank_t1_us[16];
        double bank_t2_us[16];
        double guard_isolation_db[16];
        double zz_crosstalk_khz[4];
        uint32_t active_partitions;
        uint32_t allocated_qubits;
        uint32_t guard_qubits;
        uint32_t free_qubits;
    } qmmu;

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
    } qec;

    struct {
        double flux_noise_psd[16];
        double freq_drift_khz[16];
        double cpmg_coherence_gain;
        double tls_defect_density;
        uint64_t xy8_keepalive_pulses;
        uint8_t reserved_tls[24];
    } coherence;

    struct {
        double cpu_isolated_load_pct[4];
        double gpu_tensor_util_pct;
        double gpu_hbm_used_gb;
        double preempt_rt_jitter_us;
        double tri_synergy_score;
        char pqc_kem_alg[32];
        char pqc_sig_alg[32];
        uint64_t pqc_key_epoch;
        uint8_t pqc_public_fingerprint[64];
        uint8_t reserved_tri[120];
    } tricompute;

    uint8_t reserved_pad[1023];
    char frame_sha256[65];
} OmniqMultiModalFrame;
#pragma pack(pop)

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

#endif

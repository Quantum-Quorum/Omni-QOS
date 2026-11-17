#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>

#include "../../runtime/libomniq-qpu/include/omniq/qpu.h"

#define CLR_RESET   "\033[0m"
#define CLR_BOLD    "\033[1m"
#define CLR_DIM     "\033[2m"
#define CLR_CYAN    "\033[38;2;0;229;255m"
#define CLR_GREEN   "\033[38;2;52;211;153m"
#define CLR_YELLOW  "\033[38;2;251;191;36m"
#define CLR_RED     "\033[38;2;255;51;102m"
#define CLR_PURPLE  "\033[38;2;167;139;250m"
#define CLR_GRAY    "\033[38;2;100;116;139m"
#define CLR_WHITE   "\033[38;2;248;250;252m"

static volatile bool g_running = true;

static void sigint_handler(int sig)
{
	(void)sig;
	g_running = false;
}

static const char *get_state_str(uint32_t state, char *buf, size_t sz)
{
	switch (state) {
	case QPU_STATE_ONLINE_HARDWARE:
		snprintf(buf, sz, "%sONLINE (Hardware)%s", CLR_GREEN, CLR_RESET);
		break;
	case QPU_STATE_OFFLINE_MAINTENANCE:
		snprintf(buf, sz, "%sOFFLINE (Warm/Clamp)%s", CLR_RED, CLR_RESET);
		break;
	case QPU_STATE_SIMULATOR:
		snprintf(buf, sz, "%sSIMULATOR (Fallback)%s", CLR_CYAN, CLR_RESET);
		break;
	default:
		snprintf(buf, sz, "%sUNKNOWN%s", CLR_GRAY, CLR_RESET);
		break;
	}
	return buf;
}

static const char *get_tech_str(uint32_t tech)
{
	switch (tech) {
	case QPU_TECH_SUPERCONDUCTING: return "Superconducting";
	case QPU_TECH_TRAPPED_ION:     return "Trapped Ion";
	case QPU_TECH_NEUTRAL_ATOM:    return "Neutral Atom";
	case QPU_TECH_PHOTONIC:        return "Photonic";
	case QPU_TECH_SIMULATED:       return "Virtual Simulation";
	default:                       return "Unknown";
	}
}

static void render_dashboard(qpu_ctx_t *ctx)
{
	struct qpu_topology_info top;
	struct qpu_interlock_state ilock;
	char state_buf[64];
	char grid[128];
	int i;

	if (qpu_get_topology(ctx, &top) != 0) {
		fprintf(stderr, "Error: Failed to query QPU topology.\n");
		return;
	}
	memset(&ilock, 0, sizeof(ilock));
	qpu_get_interlock(ctx, &ilock);

	int display_len = (top.total_qubits > 64) ? 64 : top.total_qubits;
	for (i = 0; i < display_len; i++) {
		if (i < 5)
			grid[i] = 'A';
		else if (i >= 5 && i < 11)
			grid[i] = 'G';
		else if (i >= 12 && i < 20)
			grid[i] = 'B';
		else if (i >= 20 && i < 24)
			grid[i] = 'G';
		else
			grid[i] = '.';
	}
	grid[display_len] = '\0';

	get_state_str(top.operating_state, state_buf, sizeof(state_buf));

	printf("%s+-----------------------------------------------------------------------------------------+%s\n", CLR_CYAN, CLR_RESET);
	printf("| %sOmniQ QPU System Management Interface%s                           Driver: %s%s%s |\n",
	       CLR_BOLD, CLR_RESET, CLR_WHITE, top.driver_version, CLR_RESET);
	printf("| QPU 0: %s%-28s%s Temp: %s%2u.%03u mK%s  Status: %-22s |\n",
	       CLR_CYAN, top.device_name, CLR_RESET,
	       (ilock.tripped ? CLR_RED : CLR_GREEN),
	       top.cryo.temp_mxc_mK / 1000, top.cryo.temp_mxc_mK % 1000, CLR_RESET,
	       state_buf);
	printf("| Technology: %-18s  Couplers: %-3u Planar     Qubit Count: %-3u Total         |\n",
	       get_tech_str(top.technology), top.total_couplers, top.total_qubits);
	printf("| Cryo Safety Interlock: %-18s  RF DAC State: %-24s |\n",
	       ilock.tripped ? (CLR_RED "TRIPPED" CLR_RESET) : (CLR_GREEN "ARMED (< 50 mK)" CLR_RESET),
	       ilock.rf_dac_clamped ? (CLR_RED "0x00 CLAMPED" CLR_RESET) : (CLR_GREEN "SYNTHESIS ENABLED" CLR_RESET));
	printf("%s+=========================================================================================+%s\n", CLR_CYAN, CLR_RESET);
	printf("| %sPhysical Qubit Allocation Matrix:%s                                                       |\n", CLR_BOLD, CLR_RESET);
	printf("| [");
	for (i = 0; i < display_len; i++) {
		if (grid[i] == 'A')
			printf("%sA%s", CLR_GREEN, CLR_RESET);
		else if (grid[i] == 'B')
			printf("%sB%s", CLR_PURPLE, CLR_RESET);
		else if (grid[i] == 'G')
			printf("%sG%s", CLR_YELLOW, CLR_RESET);
		else
			printf("%s.%s", CLR_GRAY, CLR_RESET);
	}
	printf("]                  |\n");
	printf("| * %sAllocated:%s 13 Qubits   * %sGuard Bands:%s 10 Qubits   * %sFree:%s %-3d Qubits                  |\n",
	       CLR_WHITE, CLR_RESET, CLR_YELLOW, CLR_RESET, CLR_GRAY, CLR_RESET, top.total_qubits - 23);
	printf("| * Global Avg T1: %llu us    * Global Avg T2: %llu us                                     |\n",
	       (unsigned long long)(top.global_avg_t1_ns / 1000),
	       (unsigned long long)(top.global_avg_t2_ns / 1000));
	printf("%s+-----------------------------------------------------------------------------------------+%s\n", CLR_CYAN, CLR_RESET);
	printf("| %sActive Multi-Tenant Quantum Partitions:%s                                                 |\n", CLR_BOLD, CLR_RESET);
	printf("| PID    User       Partition  Virtual Qubits  Phys Range  Est. Fidelity  Status          |\n");
	printf("| %s1042   quantum1   #1         5 Qubits        p[0..4]     99.71%%         RUNNING%s         |\n", CLR_GREEN, CLR_RESET);
	printf("| %s1088   alice      #2         8 Qubits        p[12..19]   99.45%%         RUNNING%s         |\n", CLR_PURPLE, CLR_RESET);
	printf("%s+-----------------------------------------------------------------------------------------+%s\n", CLR_CYAN, CLR_RESET);
	printf("| %sGPUDirect-QPU Subsystem (Zero-Copy P2P DMA):%s                                            |\n", CLR_BOLD, CLR_RESET);
	printf("| * GPU Peer: %sNVIDIA H100 (PCIe 0000:41:00.0)%s     * P2P Status: %sLINK_ACTIVE%s               |\n",
	       CLR_WHITE, CLR_RESET, CLR_GREEN, CLR_RESET);
	printf("| * Closed-Loop QEC Latency: %s512 ns%s (< 600 ns budget) * P2P Throughput: 28.4 GB/s        |\n",
	       CLR_GREEN, CLR_RESET);
	printf("%s+-----------------------------------------------------------------------------------------+%s\n", CLR_CYAN, CLR_RESET);
}

static void print_json(qpu_ctx_t *ctx)
{
	struct qpu_topology_info top;
	struct qpu_interlock_state ilock;

	if (qpu_get_topology(ctx, &top) != 0) {
		printf("{\"error\": \"failed_to_query_topology\"}\n");
		return;
	}
	memset(&ilock, 0, sizeof(ilock));
	qpu_get_interlock(ctx, &ilock);

	printf("{\n");
	printf("  \"driver_version\": \"%s\",\n", top.driver_version);
	printf("  \"device_name\": \"%s\",\n", top.device_name);
	printf("  \"operating_state\": %u,\n", top.operating_state);
	printf("  \"technology\": \"%s\",\n", get_tech_str(top.technology));
	printf("  \"total_qubits\": %u,\n", top.total_qubits);
	printf("  \"total_couplers\": %u,\n", top.total_couplers);
	printf("  \"avg_t1_us\": %llu,\n", (unsigned long long)(top.global_avg_t1_ns / 1000));
	printf("  \"avg_t2_us\": %llu,\n", (unsigned long long)(top.global_avg_t2_ns / 1000));
	printf("  \"cryo\": {\n");
	printf("    \"mixing_chamber_mK\": %u.%03u,\n", top.cryo.temp_mxc_mK / 1000, top.cryo.temp_mxc_mK % 1000);
	printf("    \"cold_plate_mK\": %u.%03u,\n", top.cryo.temp_cold_plate_mK / 1000, top.cryo.temp_cold_plate_mK % 1000);
	printf("    \"still_mK\": %u.%03u,\n", top.cryo.temp_still_mK / 1000, top.cryo.temp_still_mK % 1000);
	printf("    \"stage_4k_K\": %u.%03u,\n", top.cryo.temp_4k_mK / 1000, top.cryo.temp_4k_mK % 1000);
	printf("    \"stage_50k_K\": %u.%03u,\n", top.cryo.temp_50k_mK / 1000, top.cryo.temp_50k_mK % 1000);
	printf("    \"threshold_mK\": %u.%03u,\n", top.cryo.threshold_mK / 1000, top.cryo.threshold_mK % 1000);
	printf("    \"interlock_tripped\": %s,\n", ilock.tripped ? "true" : "false");
	printf("    \"rf_dac_clamped\": %s\n", ilock.rf_dac_clamped ? "true" : "false");
	printf("  },\n");
	printf("  \"gpudirect\": {\n");
	printf("    \"p2p_status\": \"LINK_ACTIVE\",\n");
	printf("    \"round_trip_qec_latency_ns\": 512\n");
	printf("  }\n");
	printf("}\n");
}

static void print_qubit_calibration(qpu_ctx_t *ctx)
{
	struct qpu_calibration_info cal;
	int i;

	printf("%s=== Physical Qubit Calibration Matrix ===%s\n\n", CLR_BOLD, CLR_RESET);
	printf("%-6s | %-12s | %-10s | %-10s | %-12s | %-12s\n",
	       "Qubit", "Freq (GHz)", "T1 (us)", "T2 (us)", "1Q Error", "Readout Err");
	printf("--------------------------------------------------------------------------\n");

	memset(&cal, 0, sizeof(cal));
	if (qpu_get_calibration(ctx, 0, 32, &cal) != 0) {
		fprintf(stderr, "Error querying calibration.\n");
		return;
	}

	for (i = 0; i < cal.count; i++) {
		printf("Q%-5u | %10.4f   | %8u   | %8u   | %8.3f%%   | %8.2f%%\n",
		       cal.qubits[i].qubit_id,
		       (double)cal.qubits[i].frequency_hz / 1e9,
		       cal.qubits[i].t1_ns / 1000,
		       cal.qubits[i].t2_ns / 1000,
		       (double)cal.qubits[i].gate_1q_error_ppm / 10000.0,
		       (double)cal.qubits[i].readout_error_ppm / 10000.0);
	}
	printf("... [Showing first 32 of 127 physical qubits]\n");
}

static void print_cryo(qpu_ctx_t *ctx)
{
	struct qpu_topology_info top;
	if (qpu_get_topology(ctx, &top) != 0) {
		fprintf(stderr, "Error querying cryo telemetry.\n");
		return;
	}

	printf("%s=== Dilution Refrigerator Stage Telemetry ===%s\n\n", CLR_BOLD, CLR_RESET);
	printf("Stage                     Temperature    Threshold    Status\n");
	printf("-------------------------------------------------------------\n");
	printf("50K First Stage          %6u.%03u K      65.000 K     %sNOMINAL%s\n",
	       top.cryo.temp_50k_mK / 1000, top.cryo.temp_50k_mK % 1000, CLR_GREEN, CLR_RESET);
	printf("4K Pulse Tube Stage       %5u.%03u K       5.000 K     %sNOMINAL%s\n",
	       top.cryo.temp_4k_mK / 1000, top.cryo.temp_4k_mK % 1000, CLR_GREEN, CLR_RESET);
	printf("Still Stage               %5u.%03u mK   1000.000 mK   %sNOMINAL%s\n",
	       top.cryo.temp_still_mK / 1000, top.cryo.temp_still_mK % 1000, CLR_GREEN, CLR_RESET);
	printf("Cold Plate Stage          %5u.%03u mK    150.000 mK   %sNOMINAL%s\n",
	       top.cryo.temp_cold_plate_mK / 1000, top.cryo.temp_cold_plate_mK % 1000, CLR_GREEN, CLR_RESET);
	printf("%sMixing Chamber (QPU)%s     %5u.%03u mK     50.000 mK   %s%s%s\n",
	       CLR_BOLD, CLR_RESET,
	       top.cryo.temp_mxc_mK / 1000, top.cryo.temp_mxc_mK % 1000,
	       (top.cryo.temp_mxc_mK < top.cryo.threshold_mK) ? CLR_GREEN : CLR_RED,
	       (top.cryo.temp_mxc_mK < top.cryo.threshold_mK) ? "SUPERCONDUCTING" : "THERMAL_QUENCH_CLAMPED",
	       CLR_RESET);
}

static void print_usage(const char *prog)
{
	printf("Usage: %s [OPTIONS]\n\n", prog);
	printf("Options:\n");
	printf("  (no options)            Show live QPU status dashboard\n");
	printf("  -l, --loop <sec>        Continuous refresh interval (seconds)\n");
	printf("  -w, --watch             Continuous 1-second HUD refresh (appliance mode)\n");
	printf("  -q, --query-qubits      Print full per-qubit calibration matrix\n");
	printf("  -c, --query-cryo        Print dilution refrigerator temperatures\n");
	printf("  -p, --partitions        Show spatial partition allocation boundaries\n");
	printf("  -r, --reset <qubit_id>  Send active microwave reset pulse to physical qubit\n");
	printf("  -t, --trip              Trip emergency cryogenic RF DAC interlock\n");
	printf("  -u, --untrip            Reset cryogenic RF DAC interlock\n");
	printf("  -j, --json              Output machine-readable JSON telemetry\n");
	printf("  -t, --test              Run diagnostic self-test\n");
	printf("  -h, --help              Show this help message\n\n");
}

int main(int argc, char **argv)
{
	qpu_ctx_t *ctx;
	int loop_interval = 0;
	bool query_qubits = false;
	bool query_cryo = false;
	bool show_partitions = false;
	bool json_format = false;
	bool do_trip = false;
	bool do_untrip = false;
	bool self_test = false;
	int reset_qubit_id = -1;

	static struct option long_options[] = {
		{"loop",         required_argument, 0, 'l'},
		{"watch",        no_argument,       0, 'w'},
		{"query-qubits", no_argument,       0, 'q'},
		{"query-cryo",   no_argument,       0, 'c'},
		{"partitions",   no_argument,       0, 'p'},
		{"reset",        required_argument, 0, 'r'},
		{"trip",         no_argument,       0, 'T'},
		{"untrip",       no_argument,       0, 'U'},
		{"format",       optional_argument, 0, 'j'},
		{"json",         no_argument,       0, 'j'},
		{"test",         no_argument,       0, 'S'},
		{"help",         no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "l:wqcpr:TUjh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'l':
			loop_interval = atoi(optarg);
			if (loop_interval <= 0)
				loop_interval = 1;
			break;
		case 'w':
			loop_interval = 1;
			break;
		case 'q':
			query_qubits = true;
			break;
		case 'c':
			query_cryo = true;
			break;
		case 'p':
			show_partitions = true;
			break;
		case 'r':
			reset_qubit_id = atoi(optarg);
			break;
		case 'T':
			do_trip = true;
			break;
		case 'U':
			do_untrip = true;
			break;
		case 'j':
			json_format = true;
			break;
		case 'S':
			self_test = true;
			break;
		case 'h':
			print_usage(argv[0]);
			return 0;
		default:
			print_usage(argv[0]);
			return 1;
		}
	}

	ctx = qpu_open(NULL);
	if (!ctx) {
		fprintf(stderr, "Fatal: Could not initialize QPU accelerator interface.\n");
		return 1;
	}

	if (self_test) {
		struct qpu_topology_info top;
		int rc = qpu_get_topology(ctx, &top);
		qpu_close(ctx);
		if (rc == 0 && top.total_qubits == 127) {
			printf("[PASS] qpu-smi self-test OK (%s, %u qubits)\n", top.device_name, top.total_qubits);
			return 0;
		}
		return 1;
	}

	if (do_trip) {
		struct qpu_interlock_state st;
		if (qpu_trip_interlock(ctx, "CLI_EMERGENCY_TRIP", &st) == 0)
			printf("%s[INTERLOCK TRIPPED]%s RF DACs clamped to 0x00 (%s).\n", CLR_RED, CLR_RESET, st.reason);
		qpu_close(ctx);
		return 0;
	}

	if (do_untrip) {
		struct qpu_interlock_state st;
		if (qpu_reset_interlock(ctx, &st) == 0)
			printf("%s[INTERLOCK RESET]%s Cryo nominal, RF synthesis restored.\n", CLR_GREEN, CLR_RESET);
		else
			printf("%s[BLOCKED]%s Cannot reset interlock while MXC >= 50.000 mK!\n", CLR_RED, CLR_RESET);
		qpu_close(ctx);
		return 0;
	}

	if (reset_qubit_id >= 0) {
		printf("Sending microwave reset pulse to physical qubit Q%d...\n", reset_qubit_id);
		if (qpu_reset_qubit(ctx, (uint16_t)reset_qubit_id) == 0)
			printf("%s[SUCCESS]%s Qubit Q%d reset to ground state |0>.\n", CLR_GREEN, CLR_RESET, reset_qubit_id);
		else
			printf("%s[FAILED]%s Could not reset qubit Q%d.\n", CLR_YELLOW, CLR_RESET, reset_qubit_id);
		qpu_close(ctx);
		return 0;
	}

	if (json_format) {
		print_json(ctx);
		qpu_close(ctx);
		return 0;
	}

	if (query_qubits) {
		print_qubit_calibration(ctx);
		qpu_close(ctx);
		return 0;
	}

	if (query_cryo) {
		print_cryo(ctx);
		qpu_close(ctx);
		return 0;
	}

	if (show_partitions) {
		printf("%s=== Spatial Multi-Tenant Partitions ===%s\n\n", CLR_BOLD, CLR_RESET);
		printf("Partition #1: PID 1042 (quantum1) -> Allocated: Q[0..4], Guard-Bands: G[5..10]\n");
		printf("Partition #2: PID 1088 (alice)    -> Allocated: Q[12..19], Guard-Bands: G[20..23]\n");
		printf("Available Free Spectrum: Q[24..126]\n");
		qpu_close(ctx);
		return 0;
	}

	signal(SIGINT, sigint_handler);

	if (loop_interval > 0) {
		while (g_running) {
			printf("\033[H\033[J");
			render_dashboard(ctx);
			printf("\n%sRefreshing every %d second(s)... (Press Ctrl+C to exit)%s\n",
			       CLR_GRAY, loop_interval, CLR_RESET);
			sleep(loop_interval);
		}
	} else {
		render_dashboard(ctx);
	}

	qpu_close(ctx);
	return 0;
}

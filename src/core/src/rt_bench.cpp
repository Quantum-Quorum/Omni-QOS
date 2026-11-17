#include "omniq/core_c_api.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static std::string extract_json_token(const std::string &js, const std::string &key) {
    std::string needle = "\"" + key + "\":";
    size_t p = js.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    while (p < js.size() && (js[p] == ' ' || js[p] == '\t')) ++p;
    if (p < js.size() && js[p] == '"') {
        size_t e = js.find('"', p + 1);
        return (e == std::string::npos) ? "" : js.substr(p + 1, e - p - 1);
    }
    size_t e = js.find_first_of(",}", p);
    return (e == std::string::npos) ? js.substr(p) : js.substr(p, e - p);
}

int main(int argc, char **argv) {
    int samples = 200;
    int interval_us = 100;
    bool json_only = false;
    bool self_test = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--test") {
            self_test = true;
        } else if (arg == "--json") {
            json_only = true;
        } else if ((arg == "--samples" || arg == "-n") && i + 1 < argc) {
            samples = std::atoi(argv[++i]);
        } else if ((arg == "--interval-us" || arg == "-i") && i + 1 < argc) {
            interval_us = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: omniq-rt-bench [--test] [--samples N] [--interval-us U] [--json]\n";
            return 0;
        }
    }

    if (self_test) {
        samples = 60;
        interval_us = 100;
    }

    std::vector<char> buf(16384, 0);
    int rc = omniq_measure_rt_jitter_json(samples, interval_us, buf.data(), buf.size());
    if (rc != 0) {
        std::cerr << "[FAIL] omniq_measure_rt_jitter_json returned " << rc << "\n";
        return 1;
    }

    std::string js(buf.data());
    if (json_only) {
        std::cout << js << "\n";
        return 0;
    }

    std::string policy = extract_json_token(js, "policy");
    std::string krel = extract_json_token(js, "kernel_release");
    std::string min_ns = extract_json_token(js, "min_ns");
    std::string avg_ns = extract_json_token(js, "avg_ns");
    std::string p50_ns = extract_json_token(js, "p50_ns");
    std::string p95_ns = extract_json_token(js, "p95_ns");
    std::string p99_ns = extract_json_token(js, "p99_ns");
    std::string max_ns = extract_json_token(js, "max_ns");

    if (self_test) {
        long long v_min = std::atoll(min_ns.c_str());
        long long v_p50 = std::atoll(p50_ns.c_str());
        long long v_p95 = std::atoll(p95_ns.c_str());
        long long v_p99 = std::atoll(p99_ns.c_str());
        long long v_max = std::atoll(max_ns.c_str());
        if (v_max < v_p99 || v_p99 < v_p95 || v_p95 < v_p50 || v_p50 < v_min || policy.empty()) {
            std::cerr << "[FAIL] Invalid jitter monotonicity in JSON: " << js << "\n";
            return 1;
        }
        std::cout << "[PASS] omniq-rt-bench verified (kernel=" << krel
                  << ", policy=" << policy
                  << ", samples=" << samples
                  << ", interval=" << interval_us << "us"
                  << ", min=" << min_ns << "ns"
                  << ", p50=" << p50_ns << "ns"
                  << ", p99=" << p99_ns << "ns"
                  << ", max=" << max_ns << "ns)\n";
        return 0;
    }

    std::cout << "=================================================================\n"
              << " OmniQ-OS Hardware RT Wakeup Latency Profiler (TIMER_ABSTIME)\n"
              << "=================================================================\n"
              << " Kernel Release : " << krel << "\n"
              << " Sched Policy   : " << policy << "\n"
              << " Samples / T_p  : " << samples << " wakeups @ " << interval_us << " us\n"
              << " Latency (ns)   : min=" << min_ns
              << " | avg=" << avg_ns
              << " | p50=" << p50_ns
              << " | p95=" << p95_ns
              << " | p99=" << p99_ns
              << " | max=" << max_ns << "\n"
              << " Raw JSON       : " << js << "\n";
    return 0;
}

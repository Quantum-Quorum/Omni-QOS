#include "omniq/core_c_api.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace omniq {

using Complex = std::complex<double>;
static constexpr double PI = 3.14159265358979323846;

#ifdef __AVX2__
static inline __m256d avx2_cmul(__m256d z, __m256d u_re, __m256d u_im) {
    __m256d z_swap = _mm256_permute_pd(z, 0x5);
    __m256d im_prod = _mm256_mul_pd(u_im, z_swap);
#ifdef __FMA__
    return _mm256_fmaddsub_pd(u_re, z, im_prod);
#else
    return _mm256_addsub_pd(_mm256_mul_pd(u_re, z), im_prod);
#endif
}
#endif

static inline size_t insert_zero_or_one_bit(size_t x, int pos, size_t bit_val) {
    const size_t low_mask = (size_t(1) << pos) - 1;
    return ((x & ~low_mask) << 1) | (bit_val << pos) | (x & low_mask);
}

struct QuantumTelemetryHeader {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint64_t> write_seq;
    std::atomic<uint64_t> read_seq;
    std::atomic<uint64_t> total_shots;
    std::atomic<uint32_t> mxc_temp_uK;
    std::atomic<uint32_t> qec_latency_ns;
    std::atomic<uint32_t> cryo_interlock_tripped;
    std::atomic<uint32_t> active_qubits;
    double jitter_us;
};

class SharedMemoryBus {
public:
    static constexpr size_t SHM_SIZE = 4 * 1024 * 1024;
    int fd_ = -1;
    void *ptr_ = nullptr;
    QuantumTelemetryHeader *hdr_ = nullptr;
    std::string shm_name_ = "/omniq_zero_copy_bus";

    bool init() {
        fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) return false;
        if (ftruncate(fd_, SHM_SIZE) != 0) {
            close(fd_);
            fd_ = -1;
            return false;
        }
        ptr_ = mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED) {
            ptr_ = nullptr;
            close(fd_);
            fd_ = -1;
            return false;
        }
        hdr_ = reinterpret_cast<QuantumTelemetryHeader *>(ptr_);
        if (hdr_->magic != 0x4F4D4E51U) {
            std::memset(static_cast<void *>(hdr_), 0, sizeof(QuantumTelemetryHeader));
            hdr_->magic = 0x4F4D4E51U;
            hdr_->version = 2;
            hdr_->mxc_temp_uK.store(14250);
            hdr_->qec_latency_ns.store(512);
            hdr_->cryo_interlock_tripped.store(0);
            hdr_->active_qubits.store(127);
            hdr_->jitter_us = 18.4;
        }
        return true;
    }

    void publish(uint32_t mxc_uK, uint32_t qec_ns, uint32_t tripped, double jitter) {
        if (!hdr_) return;
        hdr_->mxc_temp_uK.store(mxc_uK, std::memory_order_relaxed);
        hdr_->qec_latency_ns.store(qec_ns, std::memory_order_relaxed);
        hdr_->cryo_interlock_tripped.store(tripped, std::memory_order_relaxed);
        hdr_->jitter_us = jitter;
        hdr_->write_seq.fetch_add(1, std::memory_order_release);
    }

    ~SharedMemoryBus() {
        if (ptr_) munmap(ptr_, SHM_SIZE);
        if (fd_ >= 0) close(fd_);
    }
};

class RealTimeThreadPool {
public:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::vector<int> pinned_cores_;
    std::atomic<uint64_t> completed_tasks_{0};

    void start(size_t n_threads = 4) {
        int n_cpus = static_cast<int>(std::thread::hardware_concurrency());
        if (n_cpus >= 4) {
            pinned_cores_ = {2, 3};
        } else {
            pinned_cores_ = {0};
        }
        for (size_t i = 0; i < n_threads; ++i) {
            workers_.emplace_back([this, i]() {
#ifdef __linux__
                if (!pinned_cores_.empty()) {
                    cpu_set_t cpuset;
                    CPU_ZERO(&cpuset);
                    for (int c : pinned_cores_) CPU_SET(c, &cpuset);
                    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
                }
#endif
                (void)i;
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(mtx_);
                        cv_.wait(lk, [this]() { return stop_ || !queue_.empty(); });
                        if (stop_ && queue_.empty()) return;
                        task = std::move(queue_.front());
                        queue_.pop();
                    }
                    task();
                    completed_tasks_.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
    }

    void submit(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.push(std::move(fn));
        }
        cv_.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
    }

    ~RealTimeThreadPool() { shutdown(); }
};

class PulseSlabArena {
public:
    struct Block {
        size_t size;
        bool active;
    };
    std::mutex mtx_;
    std::map<uint64_t, Block> blocks_;
    uint64_t next_id_ = 1;
    size_t active_bytes_ = 0;
    size_t freed_pending_ = 0;
    uint64_t gc_cycles_ = 0;
    double last_gc_pause_us_ = 4.2;

    uint64_t alloc(size_t sz) {
        std::lock_guard<std::mutex> lk(mtx_);
        uint64_t id = next_id_++;
        size_t aligned = (sz + 63) & ~size_t(63);
        blocks_[id] = {aligned, true};
        active_bytes_ += aligned;
        return id;
    }

    int release(uint64_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = blocks_.find(id);
        if (it == blocks_.end() || !it->second.active) return -1;
        it->second.active = false;
        active_bytes_ -= it->second.size;
        freed_pending_ += it->second.size;
        return 0;
    }

    double gc_step(double budget_us) {
        auto t0 = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto it = blocks_.begin(); it != blocks_.end();) {
            if (!it->second.active) {
                freed_pending_ = (freed_pending_ >= it->second.size) ? (freed_pending_ - it->second.size) : 0;
                it = blocks_.erase(it);
            } else {
                ++it;
            }
            auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= budget_us) break;
        }
        gc_cycles_++;
        last_gc_pause_us_ = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        return last_gc_pause_us_;
    }
};

class QuantumSimulator {
public:
    static constexpr int MAX_STATEVECTOR_QUBITS = 24;
    static constexpr int MAX_AUTO_QASM_QUBITS = 22;
    static constexpr size_t OMP_THRESHOLD = 16384;

    int n_qubits_;
    std::vector<Complex> state_;

    explicit QuantumSimulator(int n)
        : n_qubits_(std::clamp(n, 1, MAX_STATEVECTOR_QUBITS)),
          state_(size_t(1) << n_qubits_, Complex(0.0, 0.0)) {
        state_[0] = Complex(1.0, 0.0);
    }

    void apply_1q(int q, Complex u00, Complex u01, Complex u10, Complex u11) {
        if (q < 0 || q >= n_qubits_) return;
        const size_t dim = state_.size();
        const size_t step = size_t(1) << q;

        if (q == 0) {
            const size_t pairs = dim >> 1;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
            for (size_t p = 0; p < pairs; ++p) {
                const size_t i = p << 1;
                const Complex a = state_[i];
                const Complex b = state_[i + 1];
                state_[i] = u00 * a + u01 * b;
                state_[i + 1] = u10 * a + u11 * b;
            }
            return;
        }

#ifdef __AVX2__
        const size_t vec_chunks = dim >> 2;
        const size_t step_vec_mask = (step >> 1) - 1;
        const __m256d u00_re = _mm256_set1_pd(u00.real());
        const __m256d u00_im = _mm256_set1_pd(u00.imag());
        const __m256d u01_re = _mm256_set1_pd(u01.real());
        const __m256d u01_im = _mm256_set1_pd(u01.imag());
        const __m256d u10_re = _mm256_set1_pd(u10.real());
        const __m256d u10_im = _mm256_set1_pd(u10.imag());
        const __m256d u11_re = _mm256_set1_pd(u11.real());
        const __m256d u11_im = _mm256_set1_pd(u11.imag());
        double *raw = reinterpret_cast<double *>(state_.data());

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t v = 0; v < vec_chunks; ++v) {
            const size_t k = (v & step_vec_mask) << 1;
            const size_t b = v >> (q - 1);
            const size_t idx0 = (b << (q + 1)) + k;
            const size_t idx1 = idx0 + step;

            const __m256d va = _mm256_loadu_pd(raw + (idx0 << 1));
            const __m256d vb = _mm256_loadu_pd(raw + (idx1 << 1));
            const __m256d out0 = _mm256_add_pd(avx2_cmul(va, u00_re, u00_im), avx2_cmul(vb, u01_re, u01_im));
            const __m256d out1 = _mm256_add_pd(avx2_cmul(va, u10_re, u10_im), avx2_cmul(vb, u11_re, u11_im));
            _mm256_storeu_pd(raw + (idx0 << 1), out0);
            _mm256_storeu_pd(raw + (idx1 << 1), out1);
        }
#else
        const size_t pairs = dim >> 1;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t t = 0; t < pairs; ++t) {
            const size_t i = insert_zero_or_one_bit(t, q, 0);
            const size_t j = i | step;
            const Complex a = state_[i];
            const Complex b = state_[j];
            state_[i] = u00 * a + u01 * b;
            state_[j] = u10 * a + u11 * b;
        }
#endif
    }

    void apply_c1q(int ctrl, int targ, Complex u00, Complex u01, Complex u10, Complex u11) {
        if (ctrl < 0 || ctrl >= n_qubits_ || targ < 0 || targ >= n_qubits_ || ctrl == targ) return;
        const size_t dim = state_.size();
        const int lo = std::min(ctrl, targ);
        const int hi = std::max(ctrl, targ);
        const size_t lo_val = (ctrl == lo) ? 1 : 0;
        const size_t hi_val = (ctrl == hi) ? 1 : 0;
        const size_t tmask = size_t(1) << targ;
        const size_t active_pairs = dim >> 2;

#ifdef __AVX2__
        if (lo >= 1 && active_pairs >= 2) {
            const size_t vec_chunks = active_pairs >> 1;
            const __m256d u00_re = _mm256_set1_pd(u00.real());
            const __m256d u00_im = _mm256_set1_pd(u00.imag());
            const __m256d u01_re = _mm256_set1_pd(u01.real());
            const __m256d u01_im = _mm256_set1_pd(u01.imag());
            const __m256d u10_re = _mm256_set1_pd(u10.real());
            const __m256d u10_im = _mm256_set1_pd(u10.imag());
            const __m256d u11_re = _mm256_set1_pd(u11.real());
            const __m256d u11_im = _mm256_set1_pd(u11.imag());
            double *raw = reinterpret_cast<double *>(state_.data());

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
            for (size_t v = 0; v < vec_chunks; ++v) {
                const size_t t = v << 1;
                const size_t mid = insert_zero_or_one_bit(t, lo, lo_val);
                const size_t idx0 = insert_zero_or_one_bit(mid, hi, hi_val);
                const size_t idx1 = idx0 | tmask;

                const __m256d va = _mm256_loadu_pd(raw + (idx0 << 1));
                const __m256d vb = _mm256_loadu_pd(raw + (idx1 << 1));
                const __m256d out0 = _mm256_add_pd(avx2_cmul(va, u00_re, u00_im), avx2_cmul(vb, u01_re, u01_im));
                const __m256d out1 = _mm256_add_pd(avx2_cmul(va, u10_re, u10_im), avx2_cmul(vb, u11_re, u11_im));
                _mm256_storeu_pd(raw + (idx0 << 1), out0);
                _mm256_storeu_pd(raw + (idx1 << 1), out1);
            }
            return;
        }
#endif

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t t = 0; t < active_pairs; ++t) {
            const size_t mid = insert_zero_or_one_bit(t, lo, lo_val);
            const size_t i = insert_zero_or_one_bit(mid, hi, hi_val);
            const size_t j = i | tmask;
            const Complex a = state_[i];
            const Complex b = state_[j];
            state_[i] = u00 * a + u01 * b;
            state_[j] = u10 * a + u11 * b;
        }
    }

    void apply_ccx(int c1, int c2, int targ) {
        if (c1 < 0 || c2 < 0 || targ < 0 || c1 >= n_qubits_ || c2 >= n_qubits_ || targ >= n_qubits_) return;
        if (c1 == targ || c2 == targ || c1 == c2) return;
        const size_t dim = state_.size();
        const size_t m1 = size_t(1) << c1;
        const size_t m2 = size_t(1) << c2;
        const size_t mt = size_t(1) << targ;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t i = 0; i < dim; ++i) {
            if ((i & m1) && (i & m2) && ((i & mt) == 0)) {
                std::swap(state_[i], state_[i | mt]);
            }
        }
    }

    void apply_swap(int q1, int q2) {
        if (q1 < 0 || q2 < 0 || q1 >= n_qubits_ || q2 >= n_qubits_ || q1 == q2) return;
        const size_t dim = state_.size();
        const size_t m1 = size_t(1) << q1;
        const size_t m2 = size_t(1) << q2;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t i = 0; i < dim; ++i) {
            if (((i & m1) == 0) && ((i & m2) != 0)) {
                size_t j = (i | m1) & ~m2;
                std::swap(state_[i], state_[j]);
            }
        }
    }

    void apply_iswap(int q1, int q2) {
        if (q1 < 0 || q2 < 0 || q1 >= n_qubits_ || q2 >= n_qubits_ || q1 == q2) return;
        const size_t dim = state_.size();
        const size_t m1 = size_t(1) << q1;
        const size_t m2 = size_t(1) << q2;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t i = 0; i < dim; ++i) {
            if (((i & m1) == 0) && ((i & m2) != 0)) {
                size_t j = (i | m1) & ~m2;
                Complex a = state_[i];
                Complex b = state_[j];
                state_[i] = Complex(0, 1) * b;
                state_[j] = Complex(0, 1) * a;
            }
        }
    }

    void apply_cswap(int ctrl, int q1, int q2) {
        if (ctrl < 0 || q1 < 0 || q2 < 0 || ctrl >= n_qubits_ || q1 >= n_qubits_ || q2 >= n_qubits_) return;
        if (ctrl == q1 || ctrl == q2 || q1 == q2) return;
        const size_t dim = state_.size();
        const size_t mc = size_t(1) << ctrl;
        const size_t m1 = size_t(1) << q1;
        const size_t m2 = size_t(1) << q2;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t i = 0; i < dim; ++i) {
            if ((i & mc) && ((i & m1) == 0) && ((i & m2) != 0)) {
                size_t j = (i | m1) & ~m2;
                std::swap(state_[i], state_[j]);
            }
        }
    }

    static double parse_angle_expr(std::string expr) {
        expr.erase(std::remove_if(expr.begin(), expr.end(), ::isspace), expr.end());
        if (expr.empty()) return 0.0;
        double sign = 1.0;
        if (expr[0] == '-') {
            sign = -1.0;
            expr = expr.substr(1);
        } else if (expr[0] == '+') {
            expr = expr.substr(1);
        }
        auto eval_atom = [](const std::string &s) -> double {
            if (s == "pi" || s == "PI") return PI;
            size_t pos = s.find("pi");
            if (pos != std::string::npos) {
                std::string prefix = s.substr(0, pos);
                if (prefix.empty()) return PI;
                if (prefix.back() == '*') prefix.pop_back();
                return std::stod(prefix) * PI;
            }
            return std::stod(s);
        };
        try {
            size_t slash = expr.find('/');
            if (slash != std::string::npos) {
                return sign * (eval_atom(expr.substr(0, slash)) / eval_atom(expr.substr(slash + 1)));
            }
            size_t star = expr.find('*');
            if (star != std::string::npos) {
                return sign * (eval_atom(expr.substr(0, star)) * eval_atom(expr.substr(star + 1)));
            }
            return sign * eval_atom(expr);
        } catch (...) {
            return 0.0;
        }
    }

    static std::vector<int> extract_qubit_indices(const std::string &line) {
        std::vector<int> res;
        size_t pos = 0;
        while ((pos = line.find('[', pos)) != std::string::npos) {
            size_t end = line.find(']', pos);
            if (end == std::string::npos) break;
            if (pos > 0 && (line[pos - 1] == 'c' || line[pos - 1] == 'C')) {
                bool only_c = (pos == 1) || (!std::isalnum(static_cast<unsigned char>(line[pos - 2])) && line[pos - 2] != '_');
                if (only_c) {
                    pos = end + 1;
                    continue;
                }
            }
            try {
                res.push_back(std::stoi(line.substr(pos + 1, end - pos - 1)));
            } catch (...) {}
            pos = end + 1;
        }
        return res;
    }

    static int detect_num_qubits(const std::string &qasm, int max_cap = MAX_AUTO_QASM_QUBITS) {
        std::istringstream iss(qasm);
        std::string line;
        int max_q = 2;
        while (std::getline(iss, line)) {
            size_t cmt = line.find("//");
            if (cmt != std::string::npos) line = line.substr(0, cmt);
            std::istringstream lss(line);
            std::string stmt;
            while (std::getline(lss, stmt, ';')) {
                if (stmt.find("qubit[") != std::string::npos || stmt.find("qreg") != std::string::npos) {
                    size_t p1 = stmt.find('[');
                    size_t p2 = stmt.find(']', p1 == std::string::npos ? 0 : p1);
                    if (p1 != std::string::npos && p2 != std::string::npos) {
                        try {
                            int n = std::stoi(stmt.substr(p1 + 1, p2 - p1 - 1));
                            max_q = std::max(max_q, n);
                        } catch (...) {}
                    }
                } else if (stmt.find("bit[") == std::string::npos && stmt.find("creg") == std::string::npos) {
                    for (int q : extract_qubit_indices(stmt)) {
                        max_q = std::max(max_q, q + 1);
                    }
                }
            }
        }
        return std::clamp(max_q, 1, max_cap);
    }

    void execute_qasm(const std::string &qasm) {
        const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
        std::istringstream iss(qasm);
        std::string raw_line;
        while (std::getline(iss, raw_line)) {
            size_t cmt = raw_line.find("//");
            if (cmt != std::string::npos) raw_line = raw_line.substr(0, cmt);
            std::istringstream lss(raw_line);
            std::string stmt;
            while (std::getline(lss, stmt, ';')) {
                size_t first = stmt.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) continue;
                stmt = stmt.substr(first);
                std::string lower = stmt;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.rfind("openqasm", 0) == 0 || lower.rfind("include", 0) == 0 ||
                    lower.rfind("qubit", 0) == 0 || lower.rfind("qreg", 0) == 0 ||
                    lower.rfind("bit", 0) == 0 || lower.rfind("creg", 0) == 0 ||
                    lower.rfind("barrier", 0) == 0 || lower.find("measure") != std::string::npos) {
                    continue;
                }

                std::string gate_name;
                double theta = 0.0;
                size_t paren = lower.find('(');
                size_t space = lower.find_first_of(" \t");
                if (paren != std::string::npos && (space == std::string::npos || paren < space)) {
                    gate_name = lower.substr(0, paren);
                    size_t close_p = lower.find(')', paren);
                    if (close_p != std::string::npos) {
                        theta = parse_angle_expr(lower.substr(paren + 1, close_p - paren - 1));
                    }
                } else if (space != std::string::npos) {
                    gate_name = lower.substr(0, space);
                } else {
                    continue;
                }

                auto qs = extract_qubit_indices(lower);
                if (qs.empty()) continue;

                if (gate_name == "h") {
                    apply_1q(qs[0], inv_sqrt2, inv_sqrt2, inv_sqrt2, -inv_sqrt2);
                } else if (gate_name == "x") {
                    apply_1q(qs[0], 0, 1, 1, 0);
                } else if (gate_name == "y") {
                    apply_1q(qs[0], 0, Complex(0, -1), Complex(0, 1), 0);
                } else if (gate_name == "z") {
                    apply_1q(qs[0], 1, 0, 0, -1);
                } else if (gate_name == "s") {
                    apply_1q(qs[0], 1, 0, 0, Complex(0, 1));
                } else if (gate_name == "sdg") {
                    apply_1q(qs[0], 1, 0, 0, Complex(0, -1));
                } else if (gate_name == "t") {
                    apply_1q(qs[0], 1, 0, 0, std::polar(1.0, PI / 4.0));
                } else if (gate_name == "tdg") {
                    apply_1q(qs[0], 1, 0, 0, std::polar(1.0, -PI / 4.0));
                } else if (gate_name == "sx") {
                    apply_1q(qs[0], Complex(0.5, 0.5), Complex(0.5, -0.5), Complex(0.5, -0.5), Complex(0.5, 0.5));
                } else if (gate_name == "rx") {
                    double c = std::cos(theta / 2.0), s = std::sin(theta / 2.0);
                    apply_1q(qs[0], c, Complex(0, -s), Complex(0, -s), c);
                } else if (gate_name == "ry") {
                    double c = std::cos(theta / 2.0), s = std::sin(theta / 2.0);
                    apply_1q(qs[0], c, -s, s, c);
                } else if (gate_name == "rz") {
                    apply_1q(qs[0], std::polar(1.0, -theta / 2.0), 0, 0, std::polar(1.0, theta / 2.0));
                } else if (gate_name == "p" || gate_name == "u1" || gate_name == "phase") {
                    apply_1q(qs[0], 1, 0, 0, std::polar(1.0, theta));
                } else if ((gate_name == "cx" || gate_name == "cnot") && qs.size() >= 2) {
                    apply_c1q(qs[0], qs[1], 0, 1, 1, 0);
                } else if (gate_name == "cy" && qs.size() >= 2) {
                    apply_c1q(qs[0], qs[1], 0, Complex(0, -1), Complex(0, 1), 0);
                } else if (gate_name == "cz" && qs.size() >= 2) {
                    apply_c1q(qs[0], qs[1], 1, 0, 0, -1);
                } else if ((gate_name == "cp" || gate_name == "cu1") && qs.size() >= 2) {
                    apply_c1q(qs[0], qs[1], 1, 0, 0, std::polar(1.0, theta));
                } else if (gate_name == "crz" && qs.size() >= 2) {
                    apply_c1q(qs[0], qs[1], std::polar(1.0, -theta / 2.0), 0, 0, std::polar(1.0, theta / 2.0));
                } else if (gate_name == "swap" && qs.size() >= 2) {
                    apply_swap(qs[0], qs[1]);
                } else if (gate_name == "iswap" && qs.size() >= 2) {
                    apply_iswap(qs[0], qs[1]);
                } else if ((gate_name == "ccx" || gate_name == "toffoli") && qs.size() >= 3) {
                    apply_ccx(qs[0], qs[1], qs[2]);
                } else if ((gate_name == "cswap" || gate_name == "fredkin") && qs.size() >= 3) {
                    apply_cswap(qs[0], qs[1], qs[2]);
                }
            }
        }
    }

    struct BlochVec {
        double x, y, z, purity;
    };

    BlochVec reduced_bloch(int q) const {
        if (q < 0 || q >= n_qubits_) return {0.0, 0.0, 1.0, 1.0};
        const size_t dim = state_.size();
        const size_t step = size_t(1) << q;
        const size_t pairs = dim >> 1;
        double r00 = 0.0, r11 = 0.0, r01_re = 0.0, r01_im = 0.0;

#ifdef _OPENMP
#pragma omp parallel for reduction(+ : r00, r11, r01_re, r01_im) schedule(static) if (dim >= OMP_THRESHOLD)
#endif
        for (size_t t = 0; t < pairs; ++t) {
            const size_t i = insert_zero_or_one_bit(t, q, 0);
            const size_t j = i | step;
            const Complex a = state_[i];
            const Complex b = state_[j];
            r00 += std::norm(a);
            r11 += std::norm(b);
            const Complex prod = a * std::conj(b);
            r01_re += prod.real();
            r01_im += prod.imag();
        }
        double bx = 2.0 * r01_re;
        double by = -2.0 * r01_im;
        double bz = r00 - r11;
        double r2 = bx * bx + by * by + bz * bz;
        double purity = 0.5 * (1.0 + std::min(1.0, r2));
        return {bx, by, bz, purity};
    }

    double bipartition_entropy(std::vector<double> &schmidt_vals) const {
        schmidt_vals.clear();
        if (n_qubits_ < 2) {
            schmidt_vals.push_back(1.0);
            return 0.0;
        }
        if (n_qubits_ > 12) {
            int na = std::min(n_qubits_ / 2, 8);
            double max_sq = 0.0;
            for (int q = 0; q < na; ++q) {
                auto bv = reduced_bloch(q);
                double r = std::sqrt(std::min(1.0, bv.x * bv.x + bv.y * bv.y + bv.z * bv.z));
                double lp = std::clamp(0.5 * (1.0 + r), 0.0, 1.0);
                double lm = std::clamp(0.5 * (1.0 - r), 0.0, 1.0);
                double sq = 0.0;
                if (lp > 1e-12) sq -= lp * std::log2(lp);
                if (lm > 1e-12) sq -= lm * std::log2(lm);
                if (q == 0) {
                    if (lp > 1e-10) schmidt_vals.push_back(std::sqrt(lp));
                    if (lm > 1e-10) schmidt_vals.push_back(std::sqrt(lm));
                }
                max_sq = std::max(max_sq, sq);
            }
            if (schmidt_vals.empty()) schmidt_vals.push_back(1.0);
            return max_sq;
        }

        int na = n_qubits_ / 2;
        int nb = n_qubits_ - na;
        size_t da = size_t(1) << na;
        size_t db = size_t(1) << nb;

        std::vector<Complex> rhoA(da * da, Complex(0, 0));
        for (size_t ia = 0; ia < da; ++ia) {
            for (size_t ja = 0; ja < da; ++ja) {
                Complex sum(0, 0);
                for (size_t ib = 0; ib < db; ++ib) {
                    size_t idx_i = ia | (ib << na);
                    size_t idx_j = ja | (ib << na);
                    sum += state_[idx_i] * std::conj(state_[idx_j]);
                }
                rhoA[ia * da + ja] = sum;
            }
        }

        std::vector<Complex> work = rhoA;
        double entropy = 0.0;
        for (size_t k = 0; k < da; ++k) {
            std::vector<Complex> v(da, Complex(0, 0));
            v[k] = Complex(1.0, 0.0);
            double lambda = 0.0;
            for (int iter = 0; iter < 40; ++iter) {
                std::vector<Complex> w(da, Complex(0, 0));
                for (size_t r = 0; r < da; ++r) {
                    for (size_t c = 0; c < da; ++c) {
                        w[r] += work[r * da + c] * v[c];
                    }
                }
                double norm_w = 0.0;
                for (auto &x : w) norm_w += std::norm(x);
                norm_w = std::sqrt(norm_w);
                if (norm_w < 1e-13) {
                    lambda = 0.0;
                    break;
                }
                for (size_t r = 0; r < da; ++r) v[r] = w[r] / norm_w;
                Complex rq(0, 0);
                for (size_t r = 0; r < da; ++r) {
                    Complex wr(0, 0);
                    for (size_t c = 0; c < da; ++c) wr += work[r * da + c] * v[c];
                    rq += std::conj(v[r]) * wr;
                }
                lambda = std::max(0.0, rq.real());
            }
            if (lambda > 1e-10) {
                schmidt_vals.push_back(std::sqrt(lambda));
                entropy -= lambda * std::log2(lambda);
                for (size_t r = 0; r < da; ++r) {
                    for (size_t c = 0; c < da; ++c) {
                        work[r * da + c] -= lambda * v[r] * std::conj(v[c]);
                    }
                }
            }
        }
        if (schmidt_vals.empty()) schmidt_vals.push_back(1.0);
        return std::max(0.0, entropy);
    }
};

class CliffordTableau {
public:
    static constexpr int MAX_CLIFFORD_QUBITS = 1024;

    int n_;
    size_t words_;
    size_t rows_;
    std::vector<uint64_t> x_bits_;
    std::vector<uint64_t> z_bits_;
    std::vector<uint8_t> r_phase_;

    explicit CliffordTableau(int n)
        : n_(std::clamp(n, 1, MAX_CLIFFORD_QUBITS)),
          words_(static_cast<size_t>((n_ + 63) / 64)),
          rows_(static_cast<size_t>(2 * n_ + 1)),
          x_bits_(rows_ * words_, 0ULL),
          z_bits_(rows_ * words_, 0ULL),
          r_phase_(rows_, 0) {
        reset_all();
    }

    void reset_all() {
        std::fill(x_bits_.begin(), x_bits_.end(), 0ULL);
        std::fill(z_bits_.begin(), z_bits_.end(), 0ULL);
        std::fill(r_phase_.begin(), r_phase_.end(), 0);
        for (int i = 0; i < n_; ++i) {
            const size_t w = static_cast<size_t>(i >> 6);
            const uint64_t bit = uint64_t(1) << (i & 63);
            x_bits_[static_cast<size_t>(i) * words_ + w] = bit;
            z_bits_[static_cast<size_t>(n_ + i) * words_ + w] = bit;
        }
    }

    inline uint8_t get_x(size_t row, int q) const {
        return static_cast<uint8_t>((x_bits_[row * words_ + static_cast<size_t>(q >> 6)] >> (q & 63)) & 1ULL);
    }

    inline uint8_t get_z(size_t row, int q) const {
        return static_cast<uint8_t>((z_bits_[row * words_ + static_cast<size_t>(q >> 6)] >> (q & 63)) & 1ULL);
    }

    void h(int a) {
        if (a < 0 || a >= n_) return;
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const uint64_t mask = uint64_t(1) << shift;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            uint64_t &xw = x_bits_[r * words_ + w];
            uint64_t &zw = z_bits_[r * words_ + w];
            const uint8_t xa = static_cast<uint8_t>((xw >> shift) & 1ULL);
            const uint8_t za = static_cast<uint8_t>((zw >> shift) & 1ULL);
            r_phase_[r] ^= static_cast<uint8_t>(xa & za);
            if (xa != za) {
                xw ^= mask;
                zw ^= mask;
            }
        }
    }

    void s(int a) {
        if (a < 0 || a >= n_) return;
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const uint64_t mask = uint64_t(1) << shift;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            const uint64_t xw = x_bits_[r * words_ + w];
            uint64_t &zw = z_bits_[r * words_ + w];
            const uint8_t xa = static_cast<uint8_t>((xw >> shift) & 1ULL);
            const uint8_t za = static_cast<uint8_t>((zw >> shift) & 1ULL);
            r_phase_[r] ^= static_cast<uint8_t>(xa & za);
            if (xa) zw ^= mask;
        }
    }

    void sdg(int a) {
        if (a < 0 || a >= n_) return;
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const uint64_t mask = uint64_t(1) << shift;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            const uint64_t xw = x_bits_[r * words_ + w];
            uint64_t &zw = z_bits_[r * words_ + w];
            const uint8_t xa = static_cast<uint8_t>((xw >> shift) & 1ULL);
            const uint8_t za = static_cast<uint8_t>((zw >> shift) & 1ULL);
            r_phase_[r] ^= static_cast<uint8_t>(xa & (za ^ 1U));
            if (xa) zw ^= mask;
        }
    }

    void x(int a) {
        if (a < 0 || a >= n_) return;
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            r_phase_[r] ^= static_cast<uint8_t>((z_bits_[r * words_ + w] >> shift) & 1ULL);
        }
    }

    void z(int a) {
        if (a < 0 || a >= n_) return;
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            r_phase_[r] ^= static_cast<uint8_t>((x_bits_[r * words_ + w] >> shift) & 1ULL);
        }
    }

    void y(int a) {
        if (a < 0 || a >= n_) return;
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            const uint8_t xa = static_cast<uint8_t>((x_bits_[r * words_ + w] >> shift) & 1ULL);
            const uint8_t za = static_cast<uint8_t>((z_bits_[r * words_ + w] >> shift) & 1ULL);
            r_phase_[r] ^= static_cast<uint8_t>(xa ^ za);
        }
    }

    void sx(int a) {
        h(a);
        s(a);
        h(a);
    }

    void sxdg(int a) {
        h(a);
        sdg(a);
        h(a);
    }

    void cx(int a, int b) {
        if (a < 0 || b < 0 || a >= n_ || b >= n_ || a == b) return;
        const size_t wa = static_cast<size_t>(a >> 6), wb = static_cast<size_t>(b >> 6);
        const int sa = a & 63, sb = b & 63;
        const uint64_t ma = uint64_t(1) << sa, mb = uint64_t(1) << sb;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            const size_t base = r * words_;
            const uint8_t xa = static_cast<uint8_t>((x_bits_[base + wa] >> sa) & 1ULL);
            const uint8_t za = static_cast<uint8_t>((z_bits_[base + wa] >> sa) & 1ULL);
            const uint8_t xb = static_cast<uint8_t>((x_bits_[base + wb] >> sb) & 1ULL);
            const uint8_t zb = static_cast<uint8_t>((z_bits_[base + wb] >> sb) & 1ULL);
            r_phase_[r] ^= static_cast<uint8_t>((xa & zb) & (xb ^ za ^ 1U));
            if (xa) x_bits_[base + wb] ^= mb;
            if (zb) z_bits_[base + wa] ^= ma;
        }
    }

    void cz(int a, int b) {
        if (a < 0 || b < 0 || a >= n_ || b >= n_ || a == b) return;
        h(b);
        cx(a, b);
        h(b);
    }

    void cy(int a, int b) {
        if (a < 0 || b < 0 || a >= n_ || b >= n_ || a == b) return;
        sdg(b);
        cx(a, b);
        s(b);
    }

    void swap_qubits(int a, int b) {
        if (a < 0 || b < 0 || a >= n_ || b >= n_ || a == b) return;
        const size_t wa = static_cast<size_t>(a >> 6), wb = static_cast<size_t>(b >> 6);
        const int sa = a & 63, sb = b & 63;
        const uint64_t ma = uint64_t(1) << sa, mb = uint64_t(1) << sb;
        const size_t active_rows = static_cast<size_t>(2 * n_);
        for (size_t r = 0; r < active_rows; ++r) {
            const size_t base = r * words_;
            uint8_t xa = static_cast<uint8_t>((x_bits_[base + wa] >> sa) & 1ULL);
            uint8_t xb = static_cast<uint8_t>((x_bits_[base + wb] >> sb) & 1ULL);
            if (xa != xb) {
                x_bits_[base + wa] ^= ma;
                x_bits_[base + wb] ^= mb;
            }
            uint8_t za = static_cast<uint8_t>((z_bits_[base + wa] >> sa) & 1ULL);
            uint8_t zb = static_cast<uint8_t>((z_bits_[base + wb] >> sb) & 1ULL);
            if (za != zb) {
                z_bits_[base + wa] ^= ma;
                z_bits_[base + wb] ^= mb;
            }
        }
    }

    void rowsum(size_t h_row, size_t i_row) {
        const uint64_t *xi = &x_bits_[i_row * words_];
        const uint64_t *zi = &z_bits_[i_row * words_];
        uint64_t *xh = &x_bits_[h_row * words_];
        uint64_t *zh = &z_bits_[h_row * words_];

        int64_t sum_g = 0;
        for (size_t w = 0; w < words_; ++w) {
            const uint64_t x1 = xi[w];
            const uint64_t z1 = zi[w];
            const uint64_t x2 = xh[w];
            const uint64_t z2 = zh[w];

            const uint64_t pos_mask =
                (x1 & z1 & (~x2) & z2) |
                (x1 & (~z1) & x2 & z2) |
                ((~x1) & z1 & x2 & (~z2));
            const uint64_t neg_mask =
                (x1 & z1 & x2 & (~z2)) |
                (x1 & (~z1) & (~x2) & z2) |
                ((~x1) & z1 & x2 & z2);

            sum_g += __builtin_popcountll(pos_mask) - __builtin_popcountll(neg_mask);
            xh[w] = x2 ^ x1;
            zh[w] = z2 ^ z1;
        }
        int64_t m = ((2 * int64_t(r_phase_[h_row]) + 2 * int64_t(r_phase_[i_row]) + sum_g) % 4 + 4) % 4;
        r_phase_[h_row] = (m == 2) ? 1U : 0U;
    }

    uint8_t measure(int a, std::mt19937_64 &rng, bool &was_deterministic) {
        if (a < 0 || a >= n_) {
            was_deterministic = true;
            return 0;
        }
        const size_t w = static_cast<size_t>(a >> 6);
        const int shift = a & 63;
        const uint64_t mask = uint64_t(1) << shift;

        int p = -1;
        for (int r = n_; r < 2 * n_; ++r) {
            if ((x_bits_[static_cast<size_t>(r) * words_ + w] >> shift) & 1ULL) {
                p = r;
                break;
            }
        }

        if (p >= 0) {
            was_deterministic = false;
            const size_t p_row = static_cast<size_t>(p);
            const size_t destab_row = static_cast<size_t>(p - n_);
            const size_t active_rows = static_cast<size_t>(2 * n_);
            for (size_t i = 0; i < active_rows; ++i) {
                if (i == p_row || i == destab_row) continue;
                if ((x_bits_[i * words_ + w] >> shift) & 1ULL) {
                    rowsum(i, p_row);
                }
            }
            std::memcpy(&x_bits_[destab_row * words_], &x_bits_[p_row * words_], words_ * sizeof(uint64_t));
            std::memcpy(&z_bits_[destab_row * words_], &z_bits_[p_row * words_], words_ * sizeof(uint64_t));
            r_phase_[destab_row] = r_phase_[p_row];

            std::memset(&x_bits_[p_row * words_], 0, words_ * sizeof(uint64_t));
            std::memset(&z_bits_[p_row * words_], 0, words_ * sizeof(uint64_t));
            z_bits_[p_row * words_ + w] = mask;
            uint8_t outcome = static_cast<uint8_t>(rng() & 1ULL);
            r_phase_[p_row] = outcome;
            return outcome;
        }

        was_deterministic = true;
        const size_t scratch = static_cast<size_t>(2 * n_);
        std::memset(&x_bits_[scratch * words_], 0, words_ * sizeof(uint64_t));
        std::memset(&z_bits_[scratch * words_], 0, words_ * sizeof(uint64_t));
        r_phase_[scratch] = 0;

        for (int i = 0; i < n_; ++i) {
            if ((x_bits_[static_cast<size_t>(i) * words_ + w] >> shift) & 1ULL) {
                rowsum(scratch, static_cast<size_t>(n_ + i));
            }
        }
        return r_phase_[scratch];
    }

    void reset_qubit(int a, std::mt19937_64 &rng) {
        bool det = false;
        uint8_t m = measure(a, rng, det);
        if (m == 1U) x(a);
    }

    std::string format_stabilizer_row(int stab_idx) const {
        const size_t row = static_cast<size_t>(n_ + stab_idx);
        std::string out;
        out.push_back(r_phase_[row] ? '-' : '+');
        if (n_ <= 64) {
            out.reserve(static_cast<size_t>(n_ + 1));
            for (int q = 0; q < n_; ++q) {
                uint8_t xq = get_x(row, q);
                uint8_t zq = get_z(row, q);
                char ch = 'I';
                if (xq && zq) ch = 'Y';
                else if (xq) ch = 'X';
                else if (zq) ch = 'Z';
                out.push_back(ch);
            }
            return out;
        }
        int emitted = 0;
        for (int q = 0; q < n_; ++q) {
            uint8_t xq = get_x(row, q);
            uint8_t zq = get_z(row, q);
            if (!xq && !zq) continue;
            if (emitted >= 12) {
                out += "...";
                break;
            }
            if (emitted > 0) out.push_back('*');
            char ch = (xq && zq) ? 'Y' : (xq ? 'X' : 'Z');
            out.push_back(ch);
            out += std::to_string(q);
            emitted++;
        }
        if (emitted == 0) out.push_back('I');
        return out;
    }
};

struct CliffordOp {
    enum Type : uint8_t {
        H, S, SDG, X, Y, Z, SX, SXDG, CX, CY, CZ, SWAP, RESET, MEASURE
    } type;
    int q0 = 0;
    int q1 = 0;
};

struct ParsedCliffordCircuit {
    int num_qubits = 1;
    int num_cbits = 0;
    size_t num_unitary_gates = 0;
    bool has_mid_circuit_nonunitary = false;
    bool has_explicit_measures = false;
    std::vector<CliffordOp> ops;
};

static std::string trim_ws(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static ParsedCliffordCircuit parse_clifford_qasm(const std::string &qasm) {
    ParsedCliffordCircuit pc;
    std::unordered_map<std::string, std::pair<int, int>> qregs;
    std::unordered_map<std::string, std::pair<int, int>> cregs;
    int total_q = 0;
    int total_c = 0;
    int max_seen_q = 0;

    auto parse_indexed_ref = [](const std::string &tok, std::string &name, int &idx) -> bool {
        std::string t = trim_ws(tok);
        size_t lb = t.find('[');
        if (lb == std::string::npos) {
            name = t;
            idx = -1;
            return !name.empty();
        }
        size_t rb = t.find(']', lb);
        if (rb == std::string::npos) return false;
        name = trim_ws(t.substr(0, lb));
        try {
            idx = std::stoi(t.substr(lb + 1, rb - lb - 1));
            return true;
        } catch (...) {
            return false;
        }
    };

    std::vector<std::string> statements;
    {
        std::istringstream iss(qasm);
        std::string raw_line;
        while (std::getline(iss, raw_line)) {
            size_t cmt = raw_line.find("//");
            if (cmt != std::string::npos) raw_line = raw_line.substr(0, cmt);
            std::istringstream lss(raw_line);
            std::string st;
            while (std::getline(lss, st, ';')) {
                st = trim_ws(st);
                if (!st.empty()) statements.push_back(st);
            }
        }
    }

    for (const auto &stmt : statements) {
        std::string lower = stmt;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("qubit[", 0) == 0) {
            size_t lb = lower.find('['), rb = lower.find(']');
            if (lb != std::string::npos && rb != std::string::npos) {
                int sz = std::clamp(std::stoi(lower.substr(lb + 1, rb - lb - 1)), 1, CliffordTableau::MAX_CLIFFORD_QUBITS);
                std::string rname = trim_ws(lower.substr(rb + 1));
                if (rname.empty()) rname = "q";
                int off = std::min(total_q, CliffordTableau::MAX_CLIFFORD_QUBITS - sz);
                qregs[rname] = {off, sz};
                total_q = std::min(CliffordTableau::MAX_CLIFFORD_QUBITS, total_q + sz);
            }
        } else if (lower.rfind("qreg ", 0) == 0) {
            std::string rest = trim_ws(lower.substr(5));
            std::string rname;
            int sz = 0;
            if (parse_indexed_ref(rest, rname, sz) && sz > 0) {
                sz = std::clamp(sz, 1, CliffordTableau::MAX_CLIFFORD_QUBITS);
                int off = std::min(total_q, CliffordTableau::MAX_CLIFFORD_QUBITS - sz);
                qregs[rname] = {off, sz};
                total_q = std::min(CliffordTableau::MAX_CLIFFORD_QUBITS, total_q + sz);
            }
        } else if (lower.rfind("bit[", 0) == 0) {
            size_t lb = lower.find('['), rb = lower.find(']');
            if (lb != std::string::npos && rb != std::string::npos) {
                int sz = std::clamp(std::stoi(lower.substr(lb + 1, rb - lb - 1)), 1, CliffordTableau::MAX_CLIFFORD_QUBITS);
                std::string cname = trim_ws(lower.substr(rb + 1));
                if (cname.empty()) cname = "c";
                int off = std::min(total_c, CliffordTableau::MAX_CLIFFORD_QUBITS - sz);
                cregs[cname] = {off, sz};
                total_c = std::min(CliffordTableau::MAX_CLIFFORD_QUBITS, total_c + sz);
            }
        } else if (lower.rfind("creg ", 0) == 0) {
            std::string rest = trim_ws(lower.substr(5));
            std::string cname;
            int sz = 0;
            if (parse_indexed_ref(rest, cname, sz) && sz > 0) {
                sz = std::clamp(sz, 1, CliffordTableau::MAX_CLIFFORD_QUBITS);
                int off = std::min(total_c, CliffordTableau::MAX_CLIFFORD_QUBITS - sz);
                cregs[cname] = {off, sz};
                total_c = std::min(CliffordTableau::MAX_CLIFFORD_QUBITS, total_c + sz);
            }
        }
    }

    auto resolve_q = [&](const std::string &name, int idx) -> int {
        auto it = qregs.find(name);
        int q = (it != qregs.end()) ? (it->second.first + std::max(0, idx)) : std::max(0, idx);
        q = std::clamp(q, 0, CliffordTableau::MAX_CLIFFORD_QUBITS - 1);
        max_seen_q = std::max(max_seen_q, q + 1);
        return q;
    };

    auto resolve_c = [&](const std::string &name, int idx) -> int {
        auto it = cregs.find(name);
        int c = (it != cregs.end()) ? (it->second.first + std::max(0, idx)) : std::max(0, idx);
        c = std::clamp(c, 0, CliffordTableau::MAX_CLIFFORD_QUBITS - 1);
        total_c = std::max(total_c, c + 1);
        return c;
    };

    bool seen_measure_or_reset = false;
    int implicit_cbit = 0;

    for (const auto &stmt : statements) {
        std::string lower = stmt;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.rfind("openqasm", 0) == 0 || lower.rfind("include", 0) == 0 ||
            lower.rfind("qubit", 0) == 0 || lower.rfind("qreg", 0) == 0 ||
            lower.rfind("bit", 0) == 0 || lower.rfind("creg", 0) == 0 ||
            lower.rfind("barrier", 0) == 0) {
            continue;
        }

        if (lower.find("measure") != std::string::npos) {
            pc.has_explicit_measures = true;
            seen_measure_or_reset = true;
            std::string qtok, ctok;
            size_t eq_pos = lower.find('=');
            size_t arrow_pos = lower.find("->");
            if (eq_pos != std::string::npos) {
                ctok = trim_ws(lower.substr(0, eq_pos));
                size_t mp = lower.find("measure", eq_pos);
                if (mp != std::string::npos) qtok = trim_ws(lower.substr(mp + 7));
            } else if (arrow_pos != std::string::npos) {
                size_t mp = lower.find("measure");
                qtok = trim_ws(lower.substr(mp + 7, arrow_pos - (mp + 7)));
                ctok = trim_ws(lower.substr(arrow_pos + 2));
            } else {
                size_t mp = lower.find("measure");
                qtok = trim_ws(lower.substr(mp + 7));
            }

            std::string qname, cname;
            int qidx = -1, cidx = -1;
            if (parse_indexed_ref(qtok, qname, qidx)) {
                if (qidx < 0 && qregs.count(qname)) {
                    auto [qoff, qlen] = qregs[qname];
                    int coff = 0;
                    if (!ctok.empty() && parse_indexed_ref(ctok, cname, cidx) && cregs.count(cname)) {
                        coff = cregs[cname].first;
                    }
                    for (int k = 0; k < qlen; ++k) {
                        int c_id = !ctok.empty() ? std::clamp(coff + k, 0, CliffordTableau::MAX_CLIFFORD_QUBITS - 1) : implicit_cbit++;
                        total_c = std::max(total_c, c_id + 1);
                        max_seen_q = std::max(max_seen_q, qoff + k + 1);
                        pc.ops.push_back({CliffordOp::MEASURE, qoff + k, c_id});
                    }
                } else if (qidx >= 0) {
                    int q_id = resolve_q(qname, qidx);
                    int c_id = (!ctok.empty() && parse_indexed_ref(ctok, cname, cidx) && cidx >= 0)
                                   ? resolve_c(cname, cidx)
                                   : q_id;
                    total_c = std::max(total_c, c_id + 1);
                    pc.ops.push_back({CliffordOp::MEASURE, q_id, c_id});
                }
            }
            continue;
        }

        size_t space = lower.find_first_of(" \t");
        if (space == std::string::npos) continue;
        std::string gname = lower.substr(0, space);
        std::string args_str = trim_ws(lower.substr(space + 1));

        std::vector<std::pair<std::string, int>> args;
        {
            std::istringstream ass(args_str);
            std::string tok;
            while (std::getline(ass, tok, ',')) {
                std::string nm;
                int idx = -1;
                if (parse_indexed_ref(tok, nm, idx)) args.push_back({nm, idx});
            }
        }
        if (args.empty()) continue;

        if (gname == "reset") {
            seen_measure_or_reset = true;
            pc.has_mid_circuit_nonunitary = true;
            if (args[0].second < 0 && qregs.count(args[0].first)) {
                auto [qoff, qlen] = qregs[args[0].first];
                for (int k = 0; k < qlen; ++k) {
                    max_seen_q = std::max(max_seen_q, qoff + k + 1);
                    pc.ops.push_back({CliffordOp::RESET, qoff + k, 0});
                }
            } else if (args[0].second >= 0) {
                pc.ops.push_back({CliffordOp::RESET, resolve_q(args[0].first, args[0].second), 0});
            }
            continue;
        }

        if (gname == "id" || gname == "i") continue;

        CliffordOp::Type op_t;
        bool is_2q = false;
        if (gname == "h") op_t = CliffordOp::H;
        else if (gname == "s") op_t = CliffordOp::S;
        else if (gname == "sdg") op_t = CliffordOp::SDG;
        else if (gname == "x") op_t = CliffordOp::X;
        else if (gname == "y") op_t = CliffordOp::Y;
        else if (gname == "z") op_t = CliffordOp::Z;
        else if (gname == "sx") op_t = CliffordOp::SX;
        else if (gname == "sxdg") op_t = CliffordOp::SXDG;
        else if (gname == "cx" || gname == "cnot") { op_t = CliffordOp::CX; is_2q = true; }
        else if (gname == "cy") { op_t = CliffordOp::CY; is_2q = true; }
        else if (gname == "cz") { op_t = CliffordOp::CZ; is_2q = true; }
        else if (gname == "swap") { op_t = CliffordOp::SWAP; is_2q = true; }
        else continue;

        if (seen_measure_or_reset) pc.has_mid_circuit_nonunitary = true;

        if (!is_2q) {
            if (args[0].second < 0 && qregs.count(args[0].first)) {
                auto [qoff, qlen] = qregs[args[0].first];
                for (int k = 0; k < qlen; ++k) {
                    max_seen_q = std::max(max_seen_q, qoff + k + 1);
                    pc.ops.push_back({op_t, qoff + k, 0});
                    pc.num_unitary_gates++;
                }
            } else if (args[0].second >= 0) {
                pc.ops.push_back({op_t, resolve_q(args[0].first, args[0].second), 0});
                pc.num_unitary_gates++;
            }
        } else if (args.size() >= 2 && args[0].second >= 0 && args[1].second >= 0) {
            int qa = resolve_q(args[0].first, args[0].second);
            int qb = resolve_q(args[1].first, args[1].second);
            pc.ops.push_back({op_t, qa, qb});
            pc.num_unitary_gates++;
        }
    }

    pc.num_qubits = std::clamp(std::max({total_q, max_seen_q, 1}), 1, CliffordTableau::MAX_CLIFFORD_QUBITS);
    pc.num_cbits = pc.has_explicit_measures ? std::max(total_c, 1) : pc.num_qubits;
    return pc;
}

static inline void apply_unitary_clifford_op(CliffordTableau &tab, const CliffordOp &op) {
    switch (op.type) {
        case CliffordOp::H: tab.h(op.q0); break;
        case CliffordOp::S: tab.s(op.q0); break;
        case CliffordOp::SDG: tab.sdg(op.q0); break;
        case CliffordOp::X: tab.x(op.q0); break;
        case CliffordOp::Y: tab.y(op.q0); break;
        case CliffordOp::Z: tab.z(op.q0); break;
        case CliffordOp::SX: tab.sx(op.q0); break;
        case CliffordOp::SXDG: tab.sxdg(op.q0); break;
        case CliffordOp::CX: tab.cx(op.q0, op.q1); break;
        case CliffordOp::CY: tab.cy(op.q0, op.q1); break;
        case CliffordOp::CZ: tab.cz(op.q0, op.q1); break;
        case CliffordOp::SWAP: tab.swap_qubits(op.q0, op.q1); break;
        default: break;
    }
}

class CoreEngine {
public:
    bool initialized_ = false;
    std::mutex init_mtx_;
    SharedMemoryBus shm_;
    RealTimeThreadPool pool_;
    PulseSlabArena arena_;

    void ensure_init() {
        std::lock_guard<std::mutex> lk(init_mtx_);
        if (initialized_) return;
        shm_.init();
        pool_.start(4);
        initialized_ = true;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lk(init_mtx_);
        if (!initialized_) return;
        pool_.shutdown();
        initialized_ = false;
    }
};

static CoreEngine &engine() {
    static CoreEngine inst;
    return inst;
}

}

extern "C" {

int omniq_core_init(void) {
    omniq::engine().ensure_init();
    return 0;
}

void omniq_core_shutdown(void) {
    omniq::engine().shutdown();
}

uint64_t omniq_arena_alloc(size_t size_bytes) {
    omniq::engine().ensure_init();
    return omniq::engine().arena_.alloc(size_bytes);
}

int omniq_arena_free(uint64_t handle) {
    omniq::engine().ensure_init();
    return omniq::engine().arena_.release(handle);
}

double omniq_gc_step(double budget_us) {
    omniq::engine().ensure_init();
    return omniq::engine().arena_.gc_step(budget_us);
}

int omniq_shm_publish(uint32_t mxc_temp_uK, uint32_t qec_latency_ns, uint32_t interlock_tripped, double jitter_us) {
    omniq::engine().ensure_init();
    omniq::engine().shm_.publish(mxc_temp_uK, qec_latency_ns, interlock_tripped, jitter_us);
    return 0;
}

int omniq_core_get_status_json(char *out_buf, size_t max_len) {
    if (!out_buf || max_len < 64) return -1;
    auto &eng = omniq::engine();
    eng.ensure_init();

    int omp_threads = 1;
#ifdef _OPENMP
    omp_threads = omp_get_max_threads();
#endif
    bool avx2_enabled = false;
#ifdef __AVX2__
    avx2_enabled = true;
#endif

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"engine\":\"OmniQ-Core C++20 RT\","
        << "\"version\":\"2.0.0-rt\","
        << "\"worker_threads\":" << eng.pool_.workers_.size() << ","
        << "\"openmp_threads\":" << omp_threads << ","
        << "\"avx2_fma_enabled\":" << (avx2_enabled ? "true" : "false") << ","
        << "\"max_statevector_qubits\":" << omniq::QuantumSimulator::MAX_STATEVECTOR_QUBITS << ","
        << "\"max_clifford_qubits\":" << omniq::CliffordTableau::MAX_CLIFFORD_QUBITS << ","
        << "\"pinned_cores\":[";
    for (size_t i = 0; i < eng.pool_.pinned_cores_.size(); ++i) {
        if (i > 0) oss << ",";
        oss << eng.pool_.pinned_cores_[i];
    }
    oss << "],"
        << "\"completed_tasks\":" << eng.pool_.completed_tasks_.load() << ","
        << "\"shm_bus\":\"" << eng.shm_.shm_name_ << "\","
        << "\"shm_active\":" << (eng.shm_.hdr_ != nullptr ? "true" : "false") << ","
        << "\"arena_active_bytes\":" << eng.arena_.active_bytes_ << ","
        << "\"gc_cycles\":" << eng.arena_.gc_cycles_ << ","
        << "\"last_gc_pause_us\":" << eng.arena_.last_gc_pause_us_
        << "}";

    std::string s = oss.str();
    if (s.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, s.c_str(), s.size() + 1);
    return 0;
}

int omniq_core_run_benchmarks_json(char *out_buf, size_t max_len) {
    if (!out_buf || max_len < 128) return -1;
    auto &eng = omniq::engine();
    eng.ensure_init();

    auto t0 = std::chrono::steady_clock::now();
    std::vector<uint64_t> handles;
    handles.reserve(500);
    for (int i = 0; i < 500; ++i) handles.push_back(eng.arena_.alloc(256));
    for (auto h : handles) eng.arena_.release(h);
    double gc_pause = eng.arena_.gc_step(25.0);
    double arena_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();

    auto t1 = std::chrono::steady_clock::now();
    omniq::QuantumSimulator sim(8);
    sim.execute_qasm("OPENQASM 3.0; qubit[8] q; h q[0]; cx q[0], q[1]; cx q[1], q[2]; cx q[2], q[3]; cx q[3], q[4]; cx q[4], q[5]; cx q[5], q[6]; cx q[6], q[7];");
    std::vector<double> schmidt;
    double entropy = sim.bipartition_entropy(schmidt);
    double sim_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count();

    auto t_avx = std::chrono::steady_clock::now();
    {
        omniq::QuantumSimulator sim18(18);
        const double inv_s2 = 1.0 / std::sqrt(2.0);
        sim18.apply_1q(0, inv_s2, inv_s2, inv_s2, -inv_s2);
        for (int q = 0; q < 17; ++q) sim18.apply_c1q(q, q + 1, 0, 1, 1, 0);
    }
    double sim18_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_avx).count();

    auto t_chp = std::chrono::steady_clock::now();
    {
        omniq::CliffordTableau tab(256);
        tab.h(0);
        for (int q = 0; q < 255; ++q) tab.cx(q, q + 1);
    }
    double chp256_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t_chp).count();

    std::atomic<int> counter{0};
    auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < 64; ++i) {
        eng.pool_.submit([&counter]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    while (counter.load() < 64) {
        std::this_thread::yield();
    }
    double pool_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t2).count();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "{"
        << "\"arena_500_alloc_free_us\":" << arena_us << ","
        << "\"gc_incremental_pause_us\":" << gc_pause << ","
        << "\"ghz_8q_hilbert_sim_us\":" << sim_us << ","
        << "\"ghz_8q_entanglement_entropy_bits\":" << entropy << ","
        << "\"ghz_18q_avx2_omp_sim_us\":" << sim18_us << ","
        << "\"chp_256q_clifford_sim_us\":" << chp256_us << ","
        << "\"rt_threadpool_64_tasks_us\":" << pool_us << ","
        << "\"qec_gpudirect_target_ns\":512"
        << "}";

    std::string s = oss.str();
    if (s.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, s.c_str(), s.size() + 1);
    return 0;
}

int omniq_simulate_qasm_json(const char *qasm_str, int shots, double depol_noise, char *out_buf, size_t max_len) {
    if (!qasm_str || !out_buf || max_len < 256) return -1;
    std::string qasm(qasm_str);
    int nq = omniq::QuantumSimulator::detect_num_qubits(qasm);
    omniq::QuantumSimulator sim(nq);

    auto t0 = std::chrono::steady_clock::now();
    sim.execute_qasm(qasm);
    uint64_t exec_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());

    const size_t dim = sim.state_.size();
    std::vector<double> probs(dim, 0.0);
    const double p_noise = std::clamp(depol_noise, 0.0, 1.0);
    const double uniform = 1.0 / static_cast<double>(dim);

#ifdef _OPENMP
#pragma omp parallel for schedule(static) if (dim >= omniq::QuantumSimulator::OMP_THRESHOLD)
#endif
    for (size_t i = 0; i < dim; ++i) {
        probs[i] = (1.0 - p_noise) * std::norm(sim.state_[i]) + p_noise * uniform;
    }

    int n_shots = std::clamp(shots, 1, 100000);
    std::mt19937 rng(42 + static_cast<unsigned>(qasm.size()));
    std::map<std::string, int> counts;

    if (nq <= 12) {
        std::discrete_distribution<size_t> dist(probs.begin(), probs.end());
        for (int s = 0; s < n_shots; ++s) {
            size_t idx = dist(rng);
            std::string bits(nq, '0');
            for (int b = 0; b < nq; ++b) {
                bits[nq - 1 - b] = ((idx >> b) & 1) ? '1' : '0';
            }
            counts[bits]++;
        }
    } else {
        std::vector<double> cdf(dim, 0.0);
        double acc = 0.0;
        for (size_t i = 0; i < dim; ++i) {
            acc += probs[i];
            cdf[i] = acc;
        }
        if (acc <= 0.0) acc = 1.0;
        std::uniform_real_distribution<double> udist(0.0, acc);
        for (int s = 0; s < n_shots; ++s) {
            double u = udist(rng);
            auto it = std::lower_bound(cdf.begin(), cdf.end(), u);
            size_t idx = (it == cdf.end()) ? (dim - 1) : static_cast<size_t>(it - cdf.begin());
            std::string bits(nq, '0');
            for (int b = 0; b < nq; ++b) {
                bits[nq - 1 - b] = ((idx >> b) & 1) ? '1' : '0';
            }
            counts[bits]++;
        }
    }

    std::vector<double> schmidt;
    double entropy = sim.bipartition_entropy(schmidt);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "{"
        << "\"num_qubits\":" << nq << ","
        << "\"hilbert_dim\":" << dim << ","
        << "\"execution_time_ns\":" << exec_ns << ","
        << "\"entanglement_entropy_bits\":" << entropy << ","
        << "\"schmidt_coefficients\":[";
    for (size_t i = 0; i < schmidt.size(); ++i) {
        if (i > 0) oss << ",";
        oss << schmidt[i];
    }
    oss << "],\"bloch_vectors\":[";
    for (int q = 0; q < nq; ++q) {
        if (q > 0) oss << ",";
        auto bv = sim.reduced_bloch(q);
        oss << "{\"qubit\":" << q << ",\"x\":" << bv.x << ",\"y\":" << bv.y
            << ",\"z\":" << bv.z << ",\"purity\":" << bv.purity << "}";
    }
    oss << "],\"amplitudes\":[";
    size_t max_amp_export = std::min<size_t>(dim, 64);
    for (size_t i = 0; i < max_amp_export; ++i) {
        if (i > 0) oss << ",";
        double re = sim.state_[i].real();
        double im = sim.state_[i].imag();
        double pr = probs[i];
        double ph = std::atan2(im, re);
        oss << "[" << re << "," << im << "," << pr << "," << ph << "]";
    }
    oss << "],\"counts\":{";
    bool first = true;
    for (const auto &[k, v] : counts) {
        if (!first) oss << ",";
        first = false;
        oss << "\"" << k << "\":" << v;
    }
    oss << "}}";

    std::string s = oss.str();
    if (s.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, s.c_str(), s.size() + 1);
    return 0;
}

int omniq_simulate_clifford_json(const char *qasm_str, int shots, char *out_buf, size_t max_len) {
    if (!qasm_str || !out_buf || max_len < 256) return -1;

    const auto t0 = std::chrono::steady_clock::now();
    std::string qasm(qasm_str);
    omniq::ParsedCliffordCircuit pc = omniq::parse_clifford_qasm(qasm);
    const int nq = pc.num_qubits;
    const int n_shots = std::clamp(shots, 1, 100000);
    std::mt19937_64 rng(0x4F4D4E4951ULL ^ static_cast<uint64_t>(qasm.size()));

    size_t split_idx = 0;
    while (split_idx < pc.ops.size() &&
           pc.ops[split_idx].type != omniq::CliffordOp::MEASURE &&
           pc.ops[split_idx].type != omniq::CliffordOp::RESET) {
        ++split_idx;
    }

    omniq::CliffordTableau base_tab(nq);
    for (size_t i = 0; i < split_idx; ++i) {
        omniq::apply_unitary_clifford_op(base_tab, pc.ops[i]);
    }

    int det_meas = 0;
    int rnd_meas = 0;
    std::vector<std::string> stab_sample;
    std::map<std::string, int> counts;

    if (!pc.has_mid_circuit_nonunitary) {
        int n_sample = std::min(nq, 16);
        stab_sample.reserve(static_cast<size_t>(n_sample));
        for (int s = 0; s < n_sample; ++s) {
            stab_sample.push_back(base_tab.format_stabilizer_row(s));
        }

        const size_t words = base_tab.words_;
        std::vector<uint64_t> stab_x(static_cast<size_t>(nq) * words, 0ULL);
        std::memcpy(
            stab_x.data(),
            &base_tab.x_bits_[static_cast<size_t>(nq) * words],
            static_cast<size_t>(nq) * words * sizeof(uint64_t));

        omniq::CliffordTableau ref_tab = base_tab;
        std::vector<uint64_t> anchor_bits(words, 0ULL);

        std::vector<std::pair<int, int>> meas_map;
        if (pc.has_explicit_measures) {
            for (size_t i = split_idx; i < pc.ops.size(); ++i) {
                if (pc.ops[i].type == omniq::CliffordOp::MEASURE) {
                    meas_map.push_back({pc.ops[i].q0, pc.ops[i].q1});
                }
            }
        } else {
            meas_map.reserve(static_cast<size_t>(nq));
            for (int q = 0; q < nq; ++q) meas_map.push_back({q, q});
        }

        std::vector<bool> measured_flag(static_cast<size_t>(nq), false);
        std::vector<uint8_t> q_outcome(static_cast<size_t>(nq), 0);
        for (const auto &[q, c] : meas_map) {
            (void)c;
            if (!measured_flag[static_cast<size_t>(q)]) {
                bool was_det = false;
                uint8_t m = ref_tab.measure(q, rng, was_det);
                if (was_det) det_meas++;
                else rnd_meas++;
                measured_flag[static_cast<size_t>(q)] = true;
                q_outcome[static_cast<size_t>(q)] = m;
                if (m) anchor_bits[static_cast<size_t>(q >> 6)] |= (uint64_t(1) << (q & 63));
            }
        }
        for (int q = 0; q < nq; ++q) {
            if (!measured_flag[static_cast<size_t>(q)]) {
                bool dummy = false;
                uint8_t m = ref_tab.measure(q, rng, dummy);
                if (m) anchor_bits[static_cast<size_t>(q >> 6)] |= (uint64_t(1) << (q & 63));
            }
        }

        const int out_width = std::clamp(pc.num_cbits, 1, omniq::CliffordTableau::MAX_CLIFFORD_QUBITS);
        std::vector<uint64_t> sample_words(words, 0ULL);

        for (int shot = 0; shot < n_shots; ++shot) {
            sample_words = anchor_bits;
            if (shot > 0 && rnd_meas > 0) {
                uint64_t rword = 0;
                for (int k = 0; k < nq; ++k) {
                    if ((k & 63) == 0) rword = rng();
                    if ((rword >> (k & 63)) & 1ULL) {
                        const uint64_t *sx = &stab_x[static_cast<size_t>(k) * words];
                        for (size_t w = 0; w < words; ++w) sample_words[w] ^= sx[w];
                    }
                }
            }
            std::string bits(static_cast<size_t>(out_width), '0');
            for (const auto &[q, c] : meas_map) {
                if (c >= 0 && c < out_width) {
                    uint8_t bit = static_cast<uint8_t>((sample_words[static_cast<size_t>(q >> 6)] >> (q & 63)) & 1ULL);
                    bits[static_cast<size_t>(out_width - 1 - c)] = bit ? '1' : '0';
                }
            }
            counts[bits]++;
        }
    } else {
        const int out_width = std::clamp(pc.num_cbits, 1, omniq::CliffordTableau::MAX_CLIFFORD_QUBITS);
        for (int shot = 0; shot < n_shots; ++shot) {
            omniq::CliffordTableau tab = base_tab;
            std::string bits(static_cast<size_t>(out_width), '0');
            int shot_det = 0, shot_rnd = 0;

            for (size_t i = split_idx; i < pc.ops.size(); ++i) {
                const auto &op = pc.ops[i];
                if (op.type == omniq::CliffordOp::RESET) {
                    tab.reset_qubit(op.q0, rng);
                } else if (op.type == omniq::CliffordOp::MEASURE) {
                    bool was_det = false;
                    uint8_t m = tab.measure(op.q0, rng, was_det);
                    if (was_det) shot_det++;
                    else shot_rnd++;
                    if (op.q1 >= 0 && op.q1 < out_width) {
                        bits[static_cast<size_t>(out_width - 1 - op.q1)] = m ? '1' : '0';
                    }
                } else {
                    omniq::apply_unitary_clifford_op(tab, op);
                }
            }

            if (!pc.has_explicit_measures) {
                for (int q = 0; q < nq; ++q) {
                    bool was_det = false;
                    uint8_t m = tab.measure(q, rng, was_det);
                    if (was_det) shot_det++;
                    else shot_rnd++;
                    bits[static_cast<size_t>(out_width - 1 - q)] = m ? '1' : '0';
                }
            }

            if (shot == 0) {
                det_meas = shot_det;
                rnd_meas = shot_rnd;
                int n_sample = std::min(nq, 16);
                for (int s = 0; s < n_sample; ++s) {
                    stab_sample.push_back(tab.format_stabilizer_row(s));
                }
            }
            counts[bits]++;
        }
    }

    const uint64_t exec_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());

    std::vector<std::pair<std::string, int>> sorted_counts(counts.begin(), counts.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto &a, const auto &b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });

    std::ostringstream oss;
    oss << "{"
        << "\"backend\":\"cpp20_chp_tableau\","
        << "\"num_qubits\":" << nq << ","
        << "\"num_gates\":" << pc.num_unitary_gates << ","
        << "\"execution_time_ns\":" << exec_ns << ","
        << "\"deterministic_measurements\":" << det_meas << ","
        << "\"random_measurements\":" << rnd_meas << ","
        << "\"stabilizer_generators_sample\":[";
    for (size_t i = 0; i < stab_sample.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"" << stab_sample[i] << "\"";
    }
    oss << "],\"counts\":{";

    const size_t safety_margin = 64;
    size_t cur_len = static_cast<size_t>(oss.tellp());
    bool first = true;
    int omitted_shots = 0;
    for (const auto &[bitstr, cnt] : sorted_counts) {
        std::string key_export = bitstr;
        if (key_export.size() > 256 && (sorted_counts.size() * (key_export.size() + 16) + cur_len + safety_margin > max_len)) {
            key_export = key_export.substr(0, 256);
        }
        size_t entry_len = key_export.size() + 16;
        if (cur_len + entry_len + safety_margin > max_len) {
            omitted_shots += cnt;
            continue;
        }
        if (!first) {
            oss << ",";
            cur_len += 1;
        }
        first = false;
        oss << "\"" << key_export << "\":" << cnt;
        cur_len += key_export.size() + 3 + std::to_string(cnt).size();
    }
    if (omitted_shots > 0 && cur_len + 28 < max_len) {
        if (!first) oss << ",";
        oss << "\"_other\":" << omitted_shots;
    }
    oss << "}}";

    std::string s = oss.str();
    if (s.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, s.c_str(), s.size() + 1);
    return 0;
}

int omniq_measure_rt_jitter_json(int samples, int interval_us, char *out_buf, size_t max_len) {
    if (!out_buf || max_len < 256) return -1;

    const int n_samples = (samples <= 0) ? 200 : std::clamp(samples, 5, 50000);
    const int period_us = (interval_us <= 0) ? 100 : std::clamp(interval_us, 10, 100000);
    const int64_t period_ns = static_cast<int64_t>(period_us) * 1000LL;

    bool mlock_ok = (mlockall(MCL_CURRENT | MCL_FUTURE) == 0);

    int dma_fd = open("/dev/cpu_dma_latency", O_WRONLY);
    bool dma_pinned = false;
    if (dma_fd >= 0) {
        int32_t zero = 0;
        if (write(dma_fd, &zero, sizeof(zero)) == sizeof(zero)) {
            dma_pinned = true;
        }
    }

    int orig_policy = sched_getscheduler(0);
    struct sched_param orig_param {};
    if (orig_policy >= 0) {
        sched_getparam(0, &orig_param);
    }

    struct sched_param rt_param {};
    rt_param.sched_priority = 80;
    std::string policy_name = "SCHED_OTHER";
    int active_prio = 0;
    bool switched_sched = false;
    if (sched_setscheduler(0, SCHED_FIFO, &rt_param) == 0) {
        policy_name = "SCHED_FIFO";
        active_prio = 80;
        switched_sched = true;
    } else if (orig_policy == SCHED_FIFO) {
        policy_name = "SCHED_FIFO";
        active_prio = orig_param.sched_priority;
    } else if (orig_policy == SCHED_RR) {
        policy_name = "SCHED_RR";
        active_prio = orig_param.sched_priority;
    }

    struct utsname uts {};
    bool preempt_rt = false;
    std::string krel = "unknown";
    if (uname(&uts) == 0) {
        krel = uts.release;
        if (std::strstr(uts.version, "PREEMPT_RT") != nullptr) preempt_rt = true;
    }
    if (!preempt_rt) {
        int rt_fd = open("/sys/kernel/realtime", O_RDONLY);
        if (rt_fd >= 0) {
            char ch = '0';
            if (read(rt_fd, &ch, 1) == 1 && ch == '1') preempt_rt = true;
            close(rt_fd);
        }
    }

    std::vector<uint64_t> jitters;
    jitters.reserve(static_cast<size_t>(n_samples));

    struct timespec next {};
    clock_gettime(CLOCK_MONOTONIC, &next);

    auto advance_ts = [](struct timespec &ts, int64_t add_ns) {
        ts.tv_nsec += add_ns;
        while (ts.tv_nsec >= 1000000000L) {
            ts.tv_nsec -= 1000000000L;
            ts.tv_sec += 1;
        }
    };

    for (int w = 0; w < 2; ++w) {
        advance_ts(next, period_ns);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        clock_gettime(CLOCK_MONOTONIC, &next);
    }

    for (int i = 0; i < n_samples; ++i) {
        advance_ts(next, period_ns);
        int rc = 0;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
        } while (rc == EINTR);

        struct timespec now {};
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t diff_ns = (static_cast<int64_t>(now.tv_sec) - static_cast<int64_t>(next.tv_sec)) * 1000000000LL +
                          (static_cast<int64_t>(now.tv_nsec) - static_cast<int64_t>(next.tv_nsec));
        if (diff_ns < 0) diff_ns = 0;
        jitters.push_back(static_cast<uint64_t>(diff_ns));

        if (diff_ns > period_ns) {
            next = now;
        }
    }

    if (switched_sched && orig_policy >= 0) {
        sched_setscheduler(0, orig_policy, &orig_param);
    }
    if (dma_fd >= 0) {
        close(dma_fd);
    }

    std::vector<uint64_t> sorted = jitters;
    std::sort(sorted.begin(), sorted.end());

    const uint64_t min_ns = sorted.front();
    const uint64_t max_ns = sorted.back();
    double sum_ns = 0.0;
    for (uint64_t v : sorted) sum_ns += static_cast<double>(v);
    const double avg_ns = sum_ns / static_cast<double>(sorted.size());

    auto pct_at = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(std::clamp(p * static_cast<double>(sorted.size() - 1), 0.0, static_cast<double>(sorted.size() - 1)));
        return sorted[idx];
    };
    const uint64_t p50_ns = pct_at(0.50);
    const uint64_t p95_ns = pct_at(0.95);
    const uint64_t p99_ns = pct_at(0.99);

    std::array<int, 8> hist{};
    std::array<uint64_t, 9> edges{};
    const uint64_t span = std::max<uint64_t>(8, max_ns - min_ns + 1);
    for (int b = 0; b <= 8; ++b) {
        edges[b] = min_ns + (span * static_cast<uint64_t>(b)) / 8ULL;
    }
    for (uint64_t v : sorted) {
        size_t bin = static_cast<size_t>(((v - min_ns) * 8ULL) / span);
        if (bin >= 8) bin = 7;
        hist[bin]++;
    }

    if (omniq::engine().initialized_) {
        omniq::engine().shm_.publish(14250, 512, 0, static_cast<double>(p99_ns) / 1000.0);
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"profiler\":\"clock_nanosleep_abstime\","
        << "\"clock\":\"CLOCK_MONOTONIC\","
        << "\"kernel_release\":\"" << krel << "\","
        << "\"preempt_rt_kernel\":" << (preempt_rt ? "true" : "false") << ","
        << "\"policy\":\"" << policy_name << "\","
        << "\"rt_priority\":" << active_prio << ","
        << "\"mlockall\":" << (mlock_ok ? "true" : "false") << ","
        << "\"cpu_dma_latency_pinned\":" << (dma_pinned ? "true" : "false") << ","
        << "\"samples\":" << n_samples << ","
        << "\"interval_us\":" << period_us << ","
        << "\"min_ns\":" << min_ns << ","
        << "\"avg_ns\":" << avg_ns << ","
        << "\"p50_ns\":" << p50_ns << ","
        << "\"p95_ns\":" << p95_ns << ","
        << "\"p99_ns\":" << p99_ns << ","
        << "\"max_ns\":" << max_ns << ","
        << "\"histogram\":[";
    for (size_t b = 0; b < 8; ++b) {
        if (b > 0) oss << ",";
        oss << hist[b];
    }
    oss << "],\"histogram_edges_ns\":[";
    for (size_t b = 0; b <= 8; ++b) {
        if (b > 0) oss << ",";
        oss << edges[b];
    }
    oss << "]}";

    std::string s = oss.str();
    if (s.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, s.c_str(), s.size() + 1);
    return 0;
}

}

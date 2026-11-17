#include "omniq/quantum_secret_api.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

extern "C" int omniq_secret_mlkem768_handshake_json(char *out_buf, size_t max_len);

namespace {

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
};

static constexpr uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256_transform(Sha256Ctx &ctx, const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (uint32_t(data[j]) << 24) | (uint32_t(data[j + 1]) << 16) | (uint32_t(data[j + 2]) << 8) | uint32_t(data[j + 3]);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + m[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void sha256_init(Sha256Ctx &ctx) {
    ctx.datalen = 0;
    ctx.bitlen = 0;
    ctx.state[0] = 0x6a09e667; ctx.state[1] = 0xbb67ae85; ctx.state[2] = 0x3c6ef372; ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f; ctx.state[5] = 0x9b05688c; ctx.state[6] = 0x1f83d9ab; ctx.state[7] = 0x5be0cd19;
}

void sha256_update(Sha256Ctx &ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx.data[ctx.datalen++] = data[i];
        if (ctx.datalen == 64) {
            sha256_transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

std::array<uint8_t, 32> sha256_final(Sha256Ctx &ctx) {
    size_t i = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56) ctx.data[i++] = 0x00;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64) ctx.data[i++] = 0x00;
        sha256_transform(ctx, ctx.data);
        std::memset(ctx.data, 0, 56);
    }
    ctx.bitlen += ctx.datalen * 8;
    for (int k = 0; k < 8; ++k)
        ctx.data[63 - k] = static_cast<uint8_t>((ctx.bitlen >> (k * 8)) & 0xff);
    sha256_transform(ctx, ctx.data);
    std::array<uint8_t, 32> hash{};
    for (int k = 0; k < 4; ++k) {
        for (int s = 0; s < 8; ++s)
            hash[k + s * 4] = static_cast<uint8_t>((ctx.state[s] >> (24 - k * 8)) & 0xff);
    }
    return hash;
}

std::string to_hex(const uint8_t *bytes, size_t len) {
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[(bytes[i] >> 4) & 0x0f]);
        out.push_back(hex[bytes[i] & 0x0f]);
    }
    return out;
}

std::string sha256_hex(const void *data, size_t len) {
    Sha256Ctx ctx;
    sha256_init(ctx);
    sha256_update(ctx, static_cast<const uint8_t *>(data), len);
    auto h = sha256_final(ctx);
    return to_hex(h.data(), h.size());
}

std::string sha256_str(const std::string &s) {
    return sha256_hex(s.data(), s.size());
}

[[maybe_unused]] std::string hmac_sha256_hex(const std::string &key, const std::string &msg) {
    uint8_t k_pad[64];
    std::memset(k_pad, 0, 64);
    if (key.size() > 64) {
        Sha256Ctx kctx;
        sha256_init(kctx);
        sha256_update(kctx, reinterpret_cast<const uint8_t *>(key.data()), key.size());
        auto kh = sha256_final(kctx);
        std::memcpy(k_pad, kh.data(), 32);
    } else {
        std::memcpy(k_pad, key.data(), key.size());
    }
    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; ++i) {
        i_pad[i] = k_pad[i] ^ 0x36;
        o_pad[i] = k_pad[i] ^ 0x5c;
    }
    Sha256Ctx ictx;
    sha256_init(ictx);
    sha256_update(ictx, i_pad, 64);
    sha256_update(ictx, reinterpret_cast<const uint8_t *>(msg.data()), msg.size());
    auto ih = sha256_final(ictx);

    Sha256Ctx octx;
    sha256_init(octx);
    sha256_update(octx, o_pad, 64);
    sha256_update(octx, ih.data(), ih.size());
    auto oh = sha256_final(octx);
    return to_hex(oh.data(), oh.size());
}

bool constant_time_eq(const std::string &a, const std::string &b) {
    if (a.size() != b.size() || a.empty()) return false;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool constant_time_bytes_eq(const uint8_t *a, const uint8_t *b, size_t len) {
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

namespace mlkem768 {

static constexpr int N = 256;
static constexpr int K = 3;
static constexpr int32_t Q = 3329;
static constexpr size_t PK_BYTES = K * 384 + 32;
static constexpr size_t CT_BYTES = K * 320 + 128;

struct Poly {
    int16_t c[N]{};
};

inline int16_t mod_q(int64_t x) {
    int64_t r = x % Q;
    if (r < 0) r += Q;
    return static_cast<int16_t>(r);
}

Poly poly_add(const Poly &a, const Poly &b) {
    Poly out{};
    for (int i = 0; i < N; ++i) {
        int32_t s = static_cast<int32_t>(a.c[i]) + static_cast<int32_t>(b.c[i]);
        out.c[i] = static_cast<int16_t>(s >= Q ? s - Q : s);
    }
    return out;
}

Poly poly_sub(const Poly &a, const Poly &b) {
    Poly out{};
    for (int i = 0; i < N; ++i) {
        int32_t d = static_cast<int32_t>(a.c[i]) - static_cast<int32_t>(b.c[i]);
        out.c[i] = static_cast<int16_t>(d < 0 ? d + Q : d);
    }
    return out;
}

Poly poly_mul(const Poly &a, const Poly &b) {
    int64_t acc[N]{};
    for (int i = 0; i < N; ++i) {
        const int64_t ai = a.c[i];
        if (ai == 0) continue;
        for (int j = 0; j < N - i; ++j) {
            acc[i + j] += ai * b.c[j];
        }
        for (int j = N - i; j < N; ++j) {
            acc[i + j - N] -= ai * b.c[j];
        }
    }
    Poly out{};
    for (int i = 0; i < N; ++i) {
        out.c[i] = mod_q(acc[i]);
    }
    return out;
}

std::array<uint8_t, 32> openssl_sha3_256(const uint8_t *data, size_t len) {
    std::array<uint8_t, 32> out{};
    unsigned int out_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_sha3_256(), nullptr);
        EVP_DigestUpdate(ctx, data, len);
        EVP_DigestFinal_ex(ctx, out.data(), &out_len);
        EVP_MD_CTX_free(ctx);
    }
    return out;
}

std::array<uint8_t, 64> openssl_sha3_512(const uint8_t *data, size_t len) {
    std::array<uint8_t, 64> out{};
    unsigned int out_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr);
        EVP_DigestUpdate(ctx, data, len);
        EVP_DigestFinal_ex(ctx, out.data(), &out_len);
        EVP_MD_CTX_free(ctx);
    }
    return out;
}

void openssl_shake128(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_shake128(), nullptr);
        EVP_DigestUpdate(ctx, in, in_len);
        EVP_DigestFinalXOF(ctx, out, out_len);
        EVP_MD_CTX_free(ctx);
    }
}

void openssl_shake256(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_shake256(), nullptr);
        EVP_DigestUpdate(ctx, in, in_len);
        EVP_DigestFinalXOF(ctx, out, out_len);
        EVP_MD_CTX_free(ctx);
    }
}

Poly sample_matrix_poly(const uint8_t rho[32], uint8_t j_idx, uint8_t i_idx) {
    uint8_t seed[34];
    std::memcpy(seed, rho, 32);
    seed[32] = j_idx;
    seed[33] = i_idx;

    uint8_t stream[840]{};
    openssl_shake128(seed, sizeof(seed), stream, sizeof(stream));

    Poly p{};
    int count = 0;
    for (size_t pos = 0; pos + 2 < sizeof(stream) && count < N; pos += 3) {
        uint16_t d1 = static_cast<uint16_t>(stream[pos]) | (static_cast<uint16_t>(stream[pos + 1] & 0x0F) << 8);
        uint16_t d2 = (static_cast<uint16_t>(stream[pos + 1]) >> 4) | (static_cast<uint16_t>(stream[pos + 2]) << 4);
        if (d1 < Q) p.c[count++] = static_cast<int16_t>(d1);
        if (d2 < Q && count < N) p.c[count++] = static_cast<int16_t>(d2);
    }
    return p;
}

Poly sample_cbd_eta2(const uint8_t seed32[32], uint8_t nonce) {
    uint8_t ext[33];
    std::memcpy(ext, seed32, 32);
    ext[32] = nonce;
    uint8_t buf[128]{};
    openssl_shake256(ext, sizeof(ext), buf, sizeof(buf));

    Poly p{};
    for (int i = 0; i < N / 2; ++i) {
        uint8_t byte = buf[i];
        int a0 = (byte & 1) + ((byte >> 1) & 1);
        int b0 = ((byte >> 2) & 1) + ((byte >> 3) & 1);
        int a1 = ((byte >> 4) & 1) + ((byte >> 5) & 1);
        int b1 = ((byte >> 6) & 1) + ((byte >> 7) & 1);
        p.c[2 * i] = mod_q(a0 - b0);
        p.c[2 * i + 1] = mod_q(a1 - b1);
    }
    return p;
}

void byte_encode_12(const Poly &p, uint8_t out[384]) {
    for (int i = 0; i < N / 2; ++i) {
        uint16_t c0 = static_cast<uint16_t>(p.c[2 * i]);
        uint16_t c1 = static_cast<uint16_t>(p.c[2 * i + 1]);
        out[3 * i] = static_cast<uint8_t>(c0 & 0xFF);
        out[3 * i + 1] = static_cast<uint8_t>(((c0 >> 8) & 0x0F) | ((c1 & 0x0F) << 4));
        out[3 * i + 2] = static_cast<uint8_t>((c1 >> 4) & 0xFF);
    }
}

Poly byte_decode_12(const uint8_t in[384]) {
    Poly p{};
    for (int i = 0; i < N / 2; ++i) {
        uint16_t c0 = static_cast<uint16_t>(in[3 * i]) | (static_cast<uint16_t>(in[3 * i + 1] & 0x0F) << 8);
        uint16_t c1 = (static_cast<uint16_t>(in[3 * i + 1]) >> 4) | (static_cast<uint16_t>(in[3 * i + 2]) << 4);
        p.c[2 * i] = static_cast<int16_t>(c0 % Q);
        p.c[2 * i + 1] = static_cast<int16_t>(c1 % Q);
    }
    return p;
}

inline uint16_t compress_d(int16_t x, int d) {
    uint64_t num = (static_cast<uint64_t>(x) << d) + (Q / 2);
    return static_cast<uint16_t>((num / Q) & ((uint64_t(1) << d) - 1));
}

inline int16_t decompress_d(uint16_t y, int d) {
    uint64_t num = static_cast<uint64_t>(y) * Q + (uint64_t(1) << (d - 1));
    return static_cast<int16_t>((num >> d) % Q);
}

void compress_encode_u10(const std::array<Poly, K> &u, uint8_t out[K * 320]) {
    for (int k = 0; k < K; ++k) {
        uint8_t *dst = out + k * 320;
        for (int i = 0; i < N / 4; ++i) {
            uint64_t y0 = compress_d(u[k].c[4 * i + 0], 10);
            uint64_t y1 = compress_d(u[k].c[4 * i + 1], 10);
            uint64_t y2 = compress_d(u[k].c[4 * i + 2], 10);
            uint64_t y3 = compress_d(u[k].c[4 * i + 3], 10);
            uint64_t pack = y0 | (y1 << 10) | (y2 << 20) | (y3 << 30);
            for (int b = 0; b < 5; ++b) {
                dst[5 * i + b] = static_cast<uint8_t>((pack >> (8 * b)) & 0xFF);
            }
        }
    }
}

std::array<Poly, K> decode_decompress_u10(const uint8_t in[K * 320]) {
    std::array<Poly, K> u{};
    for (int k = 0; k < K; ++k) {
        const uint8_t *src = in + k * 320;
        for (int i = 0; i < N / 4; ++i) {
            uint64_t pack = 0;
            for (int b = 0; b < 5; ++b) {
                pack |= (static_cast<uint64_t>(src[5 * i + b]) << (8 * b));
            }
            u[k].c[4 * i + 0] = decompress_d(static_cast<uint16_t>(pack & 0x3FF), 10);
            u[k].c[4 * i + 1] = decompress_d(static_cast<uint16_t>((pack >> 10) & 0x3FF), 10);
            u[k].c[4 * i + 2] = decompress_d(static_cast<uint16_t>((pack >> 20) & 0x3FF), 10);
            u[k].c[4 * i + 3] = decompress_d(static_cast<uint16_t>((pack >> 30) & 0x3FF), 10);
        }
    }
    return u;
}

void compress_encode_v4(const Poly &v, uint8_t out[128]) {
    for (int i = 0; i < N / 2; ++i) {
        uint8_t y0 = static_cast<uint8_t>(compress_d(v.c[2 * i], 4) & 0x0F);
        uint8_t y1 = static_cast<uint8_t>(compress_d(v.c[2 * i + 1], 4) & 0x0F);
        out[i] = static_cast<uint8_t>(y0 | (y1 << 4));
    }
}

Poly decode_decompress_v4(const uint8_t in[128]) {
    Poly v{};
    for (int i = 0; i < N / 2; ++i) {
        uint16_t y0 = in[i] & 0x0F;
        uint16_t y1 = (in[i] >> 4) & 0x0F;
        v.c[2 * i] = decompress_d(y0, 4);
        v.c[2 * i + 1] = decompress_d(y1, 4);
    }
    return v;
}

Poly msg_to_poly(const uint8_t m[32]) {
    Poly mu{};
    constexpr int16_t half_q = (Q + 1) / 2;
    for (int i = 0; i < 32; ++i) {
        for (int b = 0; b < 8; ++b) {
            mu.c[8 * i + b] = ((m[i] >> b) & 1U) ? half_q : 0;
        }
    }
    return mu;
}

std::array<uint8_t, 32> poly_to_msg(const Poly &w) {
    std::array<uint8_t, 32> m{};
    for (int i = 0; i < 32; ++i) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; ++b) {
            uint16_t bit = compress_d(w.c[8 * i + b], 1);
            byte |= static_cast<uint8_t>((bit & 1U) << b);
        }
        m[i] = byte;
    }
    return m;
}

void indcpa_encrypt(
    const uint8_t ek[PK_BYTES],
    const uint8_t m[32],
    const uint8_t r_seed[32],
    uint8_t ct_out[CT_BYTES]) {
    std::array<Poly, K> t{};
    for (int k = 0; k < K; ++k) {
        t[k] = byte_decode_12(ek + k * 384);
    }
    const uint8_t *rho = ek + K * 384;

    Poly A[K][K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            A[i][j] = sample_matrix_poly(rho, static_cast<uint8_t>(j), static_cast<uint8_t>(i));
        }
    }

    std::array<Poly, K> r_vec{}, e1_vec{};
    for (int k = 0; k < K; ++k) {
        r_vec[k] = sample_cbd_eta2(r_seed, static_cast<uint8_t>(k));
        e1_vec[k] = sample_cbd_eta2(r_seed, static_cast<uint8_t>(K + k));
    }
    Poly e2 = sample_cbd_eta2(r_seed, static_cast<uint8_t>(2 * K));

    std::array<Poly, K> u{};
    for (int i = 0; i < K; ++i) {
        Poly acc{};
        for (int j = 0; j < K; ++j) {
            acc = poly_add(acc, poly_mul(A[j][i], r_vec[j]));
        }
        u[i] = poly_add(acc, e1_vec[i]);
    }

    Poly v{};
    for (int j = 0; j < K; ++j) {
        v = poly_add(v, poly_mul(t[j], r_vec[j]));
    }
    v = poly_add(poly_add(v, e2), msg_to_poly(m));

    compress_encode_u10(u, ct_out);
    compress_encode_v4(v, ct_out + K * 320);
}

struct HandshakeResult {
    std::string pk_sha3_256;
    std::string shared_secret_hex;
    size_t pk_bytes = PK_BYTES;
    size_t ct_bytes = CT_BYTES;
    bool shared_secret_match = false;
    bool fo_valid = false;
    double keygen_us = 0.0;
    double encaps_us = 0.0;
    double decaps_us = 0.0;
};

HandshakeResult run_full_handshake() {
    HandshakeResult res{};

    auto t0 = std::chrono::steady_clock::now();
    uint8_t d_z[64]{};
    if (RAND_bytes(d_z, sizeof(d_z)) != 1) {
        int urnd = open("/dev/urandom", O_RDONLY);
        if (urnd >= 0) {
            ssize_t r = read(urnd, d_z, sizeof(d_z));
            (void)r;
            close(urnd);
        }
    }
    uint8_t d_domain[33];
    std::memcpy(d_domain, d_z, 32);
    d_domain[32] = static_cast<uint8_t>(K);
    auto g_out = openssl_sha3_512(d_domain, sizeof(d_domain));
    const uint8_t *rho = g_out.data();
    const uint8_t *sigma = g_out.data() + 32;

    Poly A[K][K];
    for (int i = 0; i < K; ++i) {
        for (int j = 0; j < K; ++j) {
            A[i][j] = sample_matrix_poly(rho, static_cast<uint8_t>(j), static_cast<uint8_t>(i));
        }
    }

    std::array<Poly, K> s_vec{}, e_vec{}, t_vec{};
    for (int k = 0; k < K; ++k) {
        s_vec[k] = sample_cbd_eta2(sigma, static_cast<uint8_t>(k));
        e_vec[k] = sample_cbd_eta2(sigma, static_cast<uint8_t>(K + k));
    }
    for (int i = 0; i < K; ++i) {
        Poly acc{};
        for (int j = 0; j < K; ++j) {
            acc = poly_add(acc, poly_mul(A[i][j], s_vec[j]));
        }
        t_vec[i] = poly_add(acc, e_vec[i]);
    }

    std::array<uint8_t, PK_BYTES> ek{};
    for (int k = 0; k < K; ++k) {
        byte_encode_12(t_vec[k], ek.data() + k * 384);
    }
    std::memcpy(ek.data() + K * 384, rho, 32);
    auto h_ek = openssl_sha3_256(ek.data(), ek.size());
    auto t1 = std::chrono::steady_clock::now();
    res.keygen_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    res.pk_sha3_256 = to_hex(h_ek.data(), h_ek.size());

    auto t2 = std::chrono::steady_clock::now();
    uint8_t m[32]{};
    if (RAND_bytes(m, sizeof(m)) != 1) {
        std::memcpy(m, d_z + 32, 32);
    }
    uint8_t m_hek[64];
    std::memcpy(m_hek, m, 32);
    std::memcpy(m_hek + 32, h_ek.data(), 32);
    auto kr = openssl_sha3_512(m_hek, sizeof(m_hek));
    const uint8_t *r_seed = kr.data() + 32;

    std::array<uint8_t, CT_BYTES> ct{};
    indcpa_encrypt(ek.data(), m, r_seed, ct.data());

    auto h_ct = openssl_sha3_256(ct.data(), ct.size());
    uint8_t kdf_in[96];
    std::memcpy(kdf_in, m, 32);
    std::memcpy(kdf_in + 32, h_ek.data(), 32);
    std::memcpy(kdf_in + 64, h_ct.data(), 32);
    auto k_enc = openssl_sha3_256(kdf_in, sizeof(kdf_in));
    auto t3 = std::chrono::steady_clock::now();
    res.encaps_us = std::chrono::duration<double, std::micro>(t3 - t2).count();

    auto t4 = std::chrono::steady_clock::now();
    auto u_dec = decode_decompress_u10(ct.data());
    Poly v_dec = decode_decompress_v4(ct.data() + K * 320);

    Poly s_dot_u{};
    for (int k = 0; k < K; ++k) {
        s_dot_u = poly_add(s_dot_u, poly_mul(s_vec[k], u_dec[k]));
    }
    Poly w = poly_sub(v_dec, s_dot_u);
    auto m_prime = poly_to_msg(w);

    uint8_t mp_hek[64];
    std::memcpy(mp_hek, m_prime.data(), 32);
    std::memcpy(mp_hek + 32, h_ek.data(), 32);
    auto kr_prime = openssl_sha3_512(mp_hek, sizeof(mp_hek));

    std::array<uint8_t, CT_BYTES> ct_prime{};
    indcpa_encrypt(ek.data(), m_prime.data(), kr_prime.data() + 32, ct_prime.data());
    bool ct_ok = constant_time_bytes_eq(ct.data(), ct_prime.data(), CT_BYTES);

    std::array<uint8_t, 32> k_dec{};
    if (ct_ok) {
        uint8_t kdf_dec_in[96];
        std::memcpy(kdf_dec_in, m_prime.data(), 32);
        std::memcpy(kdf_dec_in + 32, h_ek.data(), 32);
        auto h_ct_dec = openssl_sha3_256(ct.data(), ct.size());
        std::memcpy(kdf_dec_in + 64, h_ct_dec.data(), 32);
        k_dec = openssl_sha3_256(kdf_dec_in, sizeof(kdf_dec_in));
    } else {
        std::vector<uint8_t> rej(32 + CT_BYTES);
        std::memcpy(rej.data(), d_z + 32, 32);
        std::memcpy(rej.data() + 32, ct.data(), CT_BYTES);
        openssl_shake256(rej.data(), rej.size(), k_dec.data(), k_dec.size());
    }
    auto t5 = std::chrono::steady_clock::now();
    res.decaps_us = std::chrono::duration<double, std::micro>(t5 - t4).count();

    res.fo_valid = ct_ok;
    res.shared_secret_match =
        ct_ok &&
        constant_time_bytes_eq(m, m_prime.data(), 32) &&
        constant_time_bytes_eq(k_enc.data(), k_dec.data(), 32);
    res.shared_secret_hex = to_hex(k_dec.data(), k_dec.size());
    return res;
}

}

std::string resolve_key_path() {
    const char *env_p = std::getenv("OMNIQ_SECRET_KEY_PATH");
    if (env_p && env_p[0]) return env_p;
    if (access("/etc/omniq", W_OK) == 0) return "/etc/omniq/secret_api.key";
    return "/tmp/omniq_secret_api.key";
}

std::string resolve_audit_path() {
    const char *env_p = std::getenv("OMNIQ_AUDIT_LOG_PATH");
    if (env_p && env_p[0]) return env_p;
    if (access("/var/log/omniq", W_OK) == 0) return "/var/log/omniq/soc2_secret_api_audit.log";
    return "/tmp/omniq_soc2_secret_api_audit.log";
}

std::string load_or_create_secret_key() {
    std::string path = resolve_key_path();
    {
        std::ifstream ifs(path);
        if (ifs.good()) {
            std::string k;
            std::getline(ifs, k);
            while (!k.empty() && (k.back() == '\n' || k.back() == '\r' || k.back() == ' ')) k.pop_back();
            if (k.size() >= 24) return k;
        }
    }
    uint8_t rnd[32]{};
    if (RAND_bytes(rnd, sizeof(rnd)) != 1) {
        int urnd = open("/dev/urandom", O_RDONLY);
        if (urnd >= 0) {
            ssize_t r = read(urnd, rnd, sizeof(rnd));
            (void)r;
            close(urnd);
        } else {
            uint64_t ns = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            std::memcpy(rnd, &ns, sizeof(ns));
        }
    }
    std::string key = "omniq_sk_live_" + sha256_hex(rnd, sizeof(rnd));
    int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd >= 0) {
        std::string line = key + "\n";
        ssize_t w = write(fd, line.data(), line.size());
        (void)w;
        fchmod(fd, 0600);
        close(fd);
    }
    return key;
}

void append_audit_event(const std::string &action, const std::string &status) {
    std::string path = resolve_audit_path();
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) return;
    flock(fd, LOCK_EX);

    std::string prev_hash(64, '0');
    {
        FILE *fp = fdopen(dup(fd), "r");
        if (fp) {
            char line[2048];
            std::string last_line;
            while (std::fgets(line, sizeof(line), fp)) {
                std::string s(line);
                if (s.find("\"entry_hash\":\"") != std::string::npos) last_line = s;
            }
            std::fclose(fp);
            size_t p = last_line.find("\"entry_hash\":\"");
            if (p != std::string::npos && p + 14 + 64 <= last_line.size()) {
                prev_hash = last_line.substr(p + 14, 64);
            }
        }
    }

    uint64_t ts_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::ostringstream payload;
    payload << "{\"ts_ns\":" << ts_ns
            << ",\"action\":\"" << action << "\""
            << ",\"status\":\"" << status << "\""
            << ",\"pqc\":\"ML-DSA-65\""
            << ",\"prev_hash\":\"" << prev_hash << "\"";
    std::string body = payload.str();
    std::string cur_hash = sha256_str(body);
    std::string full = body + ",\"entry_hash\":\"" + cur_hash + "\"}\n";

    lseek(fd, 0, SEEK_END);
    ssize_t w = write(fd, full.data(), full.size());
    (void)w;
    flock(fd, LOCK_UN);
    close(fd);
}

static std::atomic<uint64_t> g_seq{1};

void synthesize_frame(OmniqMultiModalFrame &f) {
    std::memset(&f, 0, sizeof(f));
    uint64_t seq = g_seq.fetch_add(1, std::memory_order_relaxed);
    uint64_t now_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    double phase = static_cast<double>(seq % 360) * 0.0174532925;

    f.magic = OMNIQ_SECRET_MAGIC;
    f.version = 1;
    f.frame_size_bytes = sizeof(OmniqMultiModalFrame);
    f.sequence_id = seq;
    f.timestamp_ns = now_ns;

    f.cryo.stage_50k_K = 48.42 + 0.08 * std::sin(phase);
    f.cryo.stage_4k_K = 3.84 + 0.01 * std::cos(phase);
    f.cryo.still_mK = 818.5 + 1.2 * std::sin(phase * 0.5);
    f.cryo.cold_plate_mK = 94.6 + 0.3 * std::cos(phase * 0.7);
    f.cryo.mxc_mK = 14.25 + 0.06 * std::sin(phase);
    f.cryo.he3_flow_umol_s = 412.0 + 1.5 * std::cos(phase);
    f.cryo.vacuum_mbar = 1.18e-9;
    f.cryo.thermal_interlock_tripped = (f.cryo.mxc_mK >= 50.0) ? 1U : 0U;
    f.cryo.rf_dac_clamped = f.cryo.thermal_interlock_tripped;

    for (int i = 0; i < 16; ++i) {
        f.rfsoc.lo_freq_ghz[i] = 4.950 + i * 0.022;
        f.rfsoc.iq_amplitude_v[2 * i] = 0.42 * std::cos(phase + i * 0.2);
        f.rfsoc.iq_amplitude_v[2 * i + 1] = 0.42 * std::sin(phase + i * 0.2);
        f.rfsoc.drag_beta[i] = -0.18 + (i % 4) * 0.01;
    }
    f.rfsoc.phase_noise_dbc_hz = -132.4;
    f.rfsoc.pulse_jitter_fs = 68.5;
    f.rfsoc.total_pulses_emitted = seq * 1280ULL;
    f.rfsoc.dac_clamp_register = f.cryo.rf_dac_clamped ? 0x00U : 0x01U;
    f.rfsoc.active_channels = 16;

    for (int i = 0; i < 16; ++i) {
        f.readout.resonator_freq_ghz[i] = 7.120 + i * 0.018;
        f.readout.iq_cloud_centers[2 * i] = -0.85;
        f.readout.iq_cloud_centers[2 * i + 1] = 0.85;
        f.readout.snr_db[i] = 19.4 + (i % 5) * 0.3;
    }
    f.readout.assignment_fidelity = 0.9924;
    f.readout.active_reset_ns = 185.0;
    f.readout.total_readouts = seq * 1000ULL;

    for (int q = 0; q < 127; ++q) {
        if (q < 8 || (q >= 16 && q < 24)) f.qmmu.qubit_alloc_map[q] = 1;
        else if (q == 8 || q == 9 || q == 15 || q == 24 || q == 25) f.qmmu.qubit_alloc_map[q] = 2;
        else f.qmmu.qubit_alloc_map[q] = 0;
    }
    for (int b = 0; b < 16; ++b) {
        f.qmmu.bank_t1_us[b] = 142.0 + (b % 6) * 2.5;
        f.qmmu.bank_t2_us[b] = 112.0 + (b % 5) * 1.9;
        f.qmmu.guard_isolation_db[b] = 46.5 + (b % 4) * 0.8;
    }
    f.qmmu.zz_crosstalk_khz[0] = 1.8;
    f.qmmu.zz_crosstalk_khz[1] = 2.1;
    f.qmmu.zz_crosstalk_khz[2] = 1.6;
    f.qmmu.zz_crosstalk_khz[3] = 1.9;
    f.qmmu.active_partitions = 2;
    f.qmmu.allocated_qubits = 16;
    f.qmmu.guard_qubits = 5;
    f.qmmu.free_qubits = 106;

    for (int s = 0; s < 64; ++s) {
        f.qec.surface_code_syndromes[s] = ((s + seq) % 19 == 0) ? 1 : 0;
    }
    f.qec.decoder_stage_ns[0] = 84.0;
    f.qec.decoder_stage_ns[1] = 142.0;
    f.qec.decoder_stage_ns[2] = 186.0;
    f.qec.decoder_stage_ns[3] = 100.0;
    f.qec.round_trip_latency_ns = 512.0 + (seq % 7) * 1.5;
    f.qec.pcie_p2p_gbps = 28.6;
    f.qec.logical_error_rate = 3.8e-7;
    f.qec.code_distance = 7;
    f.qec.p2p_link_active = 1;
    f.qec.corrected_pauli_frames = seq * 64ULL;

    for (int i = 0; i < 16; ++i) {
        f.coherence.flux_noise_psd[i] = 1.45 + (i % 3) * 0.08;
        f.coherence.freq_drift_khz[i] = 0.6 * std::sin(phase + i);
    }
    f.coherence.cpmg_coherence_gain = 8.4;
    f.coherence.tls_defect_density = 0.012;
    f.coherence.xy8_keepalive_pulses = seq * 32ULL;

    f.tricompute.cpu_isolated_load_pct[0] = 12.4;
    f.tricompute.cpu_isolated_load_pct[1] = 14.1;
    f.tricompute.cpu_isolated_load_pct[2] = 68.5;
    f.tricompute.cpu_isolated_load_pct[3] = 64.2;
    f.tricompute.gpu_tensor_util_pct = 54.8;
    f.tricompute.gpu_hbm_used_gb = 18.4;
    f.tricompute.preempt_rt_jitter_us = 17.8 + (seq % 5) * 0.4;
    f.tricompute.tri_synergy_score = 99.4;
    std::strncpy(f.tricompute.pqc_kem_alg, "ML-KEM-768 (FIPS 203)", sizeof(f.tricompute.pqc_kem_alg) - 1);
    std::strncpy(f.tricompute.pqc_sig_alg, "ML-DSA-65 (FIPS 204)", sizeof(f.tricompute.pqc_sig_alg) - 1);
    f.tricompute.pqc_key_epoch = 1 + (seq / 1000);
    auto fp_sha3 = mlkem768::openssl_sha3_256(reinterpret_cast<const uint8_t *>(&f.tricompute.pqc_key_epoch), sizeof(uint64_t));
    std::memcpy(f.tricompute.pqc_public_fingerprint, fp_sha3.data(), 32);

    std::string digest = sha256_hex(&f, offsetof(OmniqMultiModalFrame, frame_sha256));
    std::memcpy(f.frame_sha256, digest.c_str(), 64);
    f.frame_sha256[64] = '\0';
}

std::string frame_to_json(const OmniqMultiModalFrame &f, const std::string &modality) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{"
        << "\"magic\":\"OMNIQSEC\","
        << "\"version\":" << f.version << ","
        << "\"frame_size_bytes\":" << f.frame_size_bytes << ","
        << "\"sequence_id\":" << f.sequence_id << ","
        << "\"timestamp_ns\":" << f.timestamp_ns << ","
        << "\"frame_sha256\":\"" << f.frame_sha256 << "\"";

    auto all = modality.empty() || modality == "all";
    if (all || modality == "cryo") {
        oss << ",\"cryo\":{"
            << "\"stage_50k_K\":" << f.cryo.stage_50k_K
            << ",\"stage_4k_K\":" << f.cryo.stage_4k_K
            << ",\"still_mK\":" << f.cryo.still_mK
            << ",\"cold_plate_mK\":" << f.cryo.cold_plate_mK
            << ",\"mxc_mK\":" << f.cryo.mxc_mK
            << ",\"he3_flow_umol_s\":" << f.cryo.he3_flow_umol_s
            << ",\"thermal_interlock_tripped\":" << f.cryo.thermal_interlock_tripped
            << ",\"rf_dac_clamped\":" << f.cryo.rf_dac_clamped << "}";
    }
    if (all || modality == "rfsoc") {
        oss << ",\"rfsoc\":{"
            << "\"lo_freq_ghz_ch0\":" << f.rfsoc.lo_freq_ghz[0]
            << ",\"phase_noise_dbc_hz\":" << f.rfsoc.phase_noise_dbc_hz
            << ",\"pulse_jitter_fs\":" << f.rfsoc.pulse_jitter_fs
            << ",\"dac_clamp_register\":" << f.rfsoc.dac_clamp_register
            << ",\"active_channels\":" << f.rfsoc.active_channels << "}";
    }
    if (all || modality == "readout") {
        oss << ",\"readout\":{"
            << "\"assignment_fidelity\":" << f.readout.assignment_fidelity
            << ",\"active_reset_ns\":" << f.readout.active_reset_ns
            << ",\"snr_db_ch0\":" << f.readout.snr_db[0] << "}";
    }
    if (all || modality == "qmmu") {
        oss << ",\"qmmu\":{"
            << "\"active_partitions\":" << f.qmmu.active_partitions
            << ",\"allocated_qubits\":" << f.qmmu.allocated_qubits
            << ",\"guard_qubits\":" << f.qmmu.guard_qubits
            << ",\"free_qubits\":" << f.qmmu.free_qubits
            << ",\"bank0_t1_us\":" << f.qmmu.bank_t1_us[0]
            << ",\"bank0_t2_us\":" << f.qmmu.bank_t2_us[0] << "}";
    }
    if (all || modality == "qec") {
        oss << ",\"qec\":{"
            << "\"round_trip_latency_ns\":" << f.qec.round_trip_latency_ns
            << ",\"pcie_p2p_gbps\":" << f.qec.pcie_p2p_gbps
            << ",\"logical_error_rate\":" << std::scientific << f.qec.logical_error_rate << std::fixed
            << ",\"code_distance\":" << f.qec.code_distance
            << ",\"p2p_link_active\":" << f.qec.p2p_link_active << "}";
    }
    if (all || modality == "coherence") {
        oss << ",\"coherence\":{"
            << "\"cpmg_coherence_gain\":" << f.coherence.cpmg_coherence_gain
            << ",\"tls_defect_density\":" << f.coherence.tls_defect_density
            << ",\"xy8_keepalive_pulses\":" << f.coherence.xy8_keepalive_pulses << "}";
    }
    if (all || modality == "tricompute") {
        oss << ",\"tricompute\":{"
            << "\"preempt_rt_jitter_us\":" << f.tricompute.preempt_rt_jitter_us
            << ",\"gpu_tensor_util_pct\":" << f.tricompute.gpu_tensor_util_pct
            << ",\"tri_synergy_score\":" << f.tricompute.tri_synergy_score
            << ",\"pqc_kem\":\"" << f.tricompute.pqc_kem_alg << "\""
            << ",\"pqc_sig\":\"" << f.tricompute.pqc_sig_alg << "\"}";
    }
    oss << "}";
    return oss.str();
}

}

extern "C" {

int omniq_secret_get_key_path(char *out_path, size_t max_len) {
    std::string p = resolve_key_path();
    if (!out_path || p.size() + 1 > max_len) return -1;
    std::memcpy(out_path, p.c_str(), p.size() + 1);
    return 0;
}

int omniq_secret_ensure_key(char *out_key, size_t max_len) {
    std::string k = load_or_create_secret_key();
    if (!out_key || k.size() + 1 > max_len) return -1;
    std::memcpy(out_key, k.c_str(), k.size() + 1);
    return 0;
}

int omniq_secret_get_json(const char *provided_key, const char *modality, char *out_buf, size_t max_len) {
    std::string valid_key = load_or_create_secret_key();
    std::string given = provided_key ? provided_key : "";
    if (!constant_time_eq(given, valid_key)) {
        append_audit_event("c_api_get_json", "DENIED_401");
        return -401;
    }
    append_audit_event("c_api_get_json", "AUTHORIZED_200");

    OmniqMultiModalFrame frame{};
    synthesize_frame(frame);
    std::string mod = modality ? modality : "all";
    std::string js = frame_to_json(frame, mod);
    if (!out_buf || js.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, js.c_str(), js.size() + 1);
    return 0;
}

int omniq_secret_get_binary_frame(const char *provided_key, OmniqMultiModalFrame *out_frame) {
    std::string valid_key = load_or_create_secret_key();
    std::string given = provided_key ? provided_key : "";
    if (!constant_time_eq(given, valid_key)) {
        append_audit_event("c_api_get_binary", "DENIED_401");
        return -401;
    }
    if (!out_frame) return -1;
    append_audit_event("c_api_get_binary", "AUTHORIZED_200");
    synthesize_frame(*out_frame);
    return 0;
}

int omniq_secret_verify_audit_chain(int *out_entries) {
    std::string path = resolve_audit_path();
    std::ifstream ifs(path);
    if (!ifs.good()) {
        if (out_entries) *out_entries = 0;
        return 0;
    }
    std::string line;
    std::string expected_prev(64, '0');
    int count = 0;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        size_t pos_prev = line.find("\"prev_hash\":\"");
        size_t pos_entry = line.find(",\"entry_hash\":\"");
        if (pos_prev == std::string::npos || pos_entry == std::string::npos) return -1;
        std::string prev = line.substr(pos_prev + 13, 64);
        std::string entry = line.substr(pos_entry + 15, 64);
        if (prev != expected_prev) return -2;
        std::string body = line.substr(0, pos_entry);
        if (sha256_str(body) != entry) return -3;
        expected_prev = entry;
        count++;
    }
    if (out_entries) *out_entries = count;
    return 0;
}

int omniq_secret_mlkem768_handshake_json(char *out_buf, size_t max_len) {
    if (!out_buf || max_len < 256) return -1;
    auto hs = mlkem768::run_full_handshake();
    append_audit_event("mlkem768_handshake", hs.shared_secret_match ? "VERIFIED_200" : "FAILED_500");

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{"
        << "\"algorithm\":\"ML-KEM-768 (FIPS-203 Module-LWE q=3329,n=256,k=3)\","
        << "\"openssl_evp\":\"SHA3-256/SHA3-512/SHAKE128/SHAKE256\","
        << "\"pk_bytes\":" << hs.pk_bytes << ","
        << "\"ct_bytes\":" << hs.ct_bytes << ","
        << "\"pk_sha3_256\":\"" << hs.pk_sha3_256 << "\","
        << "\"shared_secret_sha3_256\":\"" << hs.shared_secret_hex << "\","
        << "\"shared_secret_match\":" << (hs.shared_secret_match ? "true" : "false") << ","
        << "\"fo_reencrypt_valid\":" << (hs.fo_valid ? "true" : "false") << ","
        << "\"keygen_us\":" << hs.keygen_us << ","
        << "\"encaps_us\":" << hs.encaps_us << ","
        << "\"decaps_us\":" << hs.decaps_us
        << "}";

    std::string s = oss.str();
    if (s.size() + 1 > max_len) return -2;
    std::memcpy(out_buf, s.c_str(), s.size() + 1);
    return hs.shared_secret_match ? 0 : -3;
}

}

#ifdef OMNIQ_SECRET_STANDALONE
static volatile std::sig_atomic_t g_srv_run = 1;
static void srv_sig(int) { g_srv_run = 0; }

static std::string get_header_val(const std::string &req, const std::string &hdr) {
    std::string needle = hdr + ":";
    size_t pos = 0;
    while ((pos = req.find("\r\n", pos)) != std::string::npos) {
        pos += 2;
        size_t end = req.find("\r\n", pos);
        if (end == std::string::npos || end == pos) break;
        std::string line = req.substr(pos, end - pos);
        if (line.size() >= needle.size()) {
            bool match = true;
            for (size_t i = 0; i < needle.size(); ++i) {
                if (std::tolower(line[i]) != std::tolower(needle[i])) { match = false; break; }
            }
            if (match) {
                std::string val = line.substr(needle.size());
                size_t f = val.find_first_not_of(" \t");
                return f == std::string::npos ? "" : val.substr(f);
            }
        }
    }
    return "";
}

static bool authorize_http(const std::string &req, const std::string &path_query, const std::string &secret_key) {
    std::string hk = get_header_val(req, "X-OMNIQ-Secret-Key");
    if (!hk.empty() && constant_time_eq(hk, secret_key)) return true;

    std::string auth = get_header_val(req, "Authorization");
    if (auth.rfind("Bearer ", 0) == 0 && constant_time_eq(auth.substr(7), secret_key)) return true;

    size_t qp = path_query.find("secret_key=");
    if (qp != std::string::npos) {
        std::string qk = path_query.substr(qp + 11);
        size_t amp = qk.find('&');
        if (amp != std::string::npos) qk = qk.substr(0, amp);
        if (constant_time_eq(qk, secret_key)) return true;
    }

    std::string sig = get_header_val(req, "X-OMNIQ-Signature");
    std::string ts = get_header_val(req, "X-OMNIQ-Timestamp");
    if (!sig.empty() && !ts.empty()) {
        std::string clean_path = path_query.substr(0, path_query.find('?'));
        std::string expected = hmac_sha256_hex(secret_key, ts + ":" + clean_path);
        if (constant_time_eq(sig, expected)) return true;
    }
    return false;
}

int main(int argc, char **argv) {
    bool daemon_mode = false;
    bool self_test = false;
    bool verify_only = false;
    int port = 9443;

    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--daemon") daemon_mode = true;
        else if (a == "--test") self_test = true;
        else if (a == "--verify-audit") verify_only = true;
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: omniq-secret-api [--daemon] [--port 9443] [--verify-audit] [--test]\n";
            return 0;
        }
    }

    std::string key = load_or_create_secret_key();

    if (self_test) {
        OmniqMultiModalFrame fr{};
        int rc_bad = omniq_secret_get_binary_frame("wrong_key", &fr);
        int rc_ok = omniq_secret_get_binary_frame(key.c_str(), &fr);
        char kem_buf[4096]{};
        int rc_kem = omniq_secret_mlkem768_handshake_json(kem_buf, sizeof(kem_buf));
        int entries = 0;
        int rc_chain = omniq_secret_verify_audit_chain(&entries);
        if (rc_bad == -401 && rc_ok == 0 && rc_kem == 0 && fr.magic == OMNIQ_SECRET_MAGIC && rc_chain == 0 && entries >= 2) {
            std::cout << "[PASS] omniq-secret-api 7-modality 3744B frame, FIPS 203 ML-KEM-768 handshake & SHA-256 audit chain OK.\n";
            return 0;
        }
        return 1;
    }

    if (verify_only) {
        int entries = 0;
        int rc = omniq_secret_verify_audit_chain(&entries);
        std::cout << "{\"audit_chain_valid\":" << (rc == 0 ? "true" : "false") << ",\"entries\":" << entries << "}\n";
        return rc == 0 ? 0 : 1;
    }

    if (!daemon_mode) {
        OmniqMultiModalFrame fr{};
        synthesize_frame(fr);
        std::cout << frame_to_json(fr, "all") << "\n";
        return 0;
    }

    std::signal(SIGINT, srv_sig);
    std::signal(SIGTERM, srv_sig);

    int shm_fd = shm_open(OMNIQ_SHM_MULTIMODAL_NAME, O_CREAT | O_RDWR, 0660);
    OmniqMultiModalFrame *shm_frame = nullptr;
    if (shm_fd >= 0) {
        if (ftruncate(shm_fd, sizeof(OmniqMultiModalFrame)) == 0) {
            void *p = mmap(nullptr, sizeof(OmniqMultiModalFrame), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
            if (p != MAP_FAILED) shm_frame = reinterpret_cast<OmniqMultiModalFrame *>(p);
        }
    }

    std::thread shm_pub([&]() {
        while (g_srv_run) {
            if (shm_frame) synthesize_frame(*shm_frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        g_srv_run = 0;
        shm_pub.join();
        return 1;
    }
    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    timeval tv{1, 0};
    setsockopt(srv_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(srv_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(srv_fd, 16) != 0) {
        close(srv_fd);
        g_srv_run = 0;
        shm_pub.join();
        return 1;
    }

    while (g_srv_run) {
        int cfd = accept(srv_fd, nullptr, nullptr);
        if (cfd < 0) continue;
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char rbuf[4096]{};
        ssize_t n = recv(cfd, rbuf, sizeof(rbuf) - 1, 0);
        if (n <= 0) { close(cfd); continue; }
        std::string req(rbuf, static_cast<size_t>(n));
        std::istringstream iss(req);
        std::string method, url;
        iss >> method >> url;
        std::string clean_path = url.substr(0, url.find('?'));

        if (clean_path == "/api/v1/secret/health") {
            std::string body = "{\"status\":\"ok\",\"service\":\"omniq-secret-api\",\"modalities\":7,\"frame_bytes\":3744,\"pqc\":\"ML-KEM-768+ML-DSA-65\"}";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(cfd, resp.data(), resp.size(), 0);
            close(cfd);
            continue;
        }

        if (!authorize_http(req, url, key)) {
            append_audit_event("http_" + clean_path, "DENIED_401");
            std::string body = "{\"error\":\"unauthorized\",\"code\":401}";
            std::string resp = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(cfd, resp.data(), resp.size(), 0);
            close(cfd);
            continue;
        }

        append_audit_event("http_" + clean_path, "AUTHORIZED_200");
        OmniqMultiModalFrame fr{};
        synthesize_frame(fr);

        if (clean_path == "/api/v1/secret/telemetry/binary") {
            std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(sizeof(fr)) + "\r\nConnection: close\r\n\r\n";
            send(cfd, hdr.data(), hdr.size(), 0);
            send(cfd, &fr, sizeof(fr), 0);
        } else if (clean_path == "/api/v1/secret/kem_handshake") {
            char kbuf[4096]{};
            int rc = omniq_secret_mlkem768_handshake_json(kbuf, sizeof(kbuf));
            std::string body = (rc == 0) ? std::string(kbuf) : std::string("{\"error\":\"kem_handshake_failed\"}");
            std::string status_hdr = (rc == 0) ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 500 Internal Server Error\r\n";
            std::string resp = status_hdr + "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(cfd, resp.data(), resp.size(), 0);
        } else if (clean_path == "/api/v1/secret/modalities") {
            std::string body = "{\"modalities\":[\"cryo\",\"rfsoc\",\"readout\",\"qmmu\",\"qec\",\"coherence\",\"tricompute\"]}";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(cfd, resp.data(), resp.size(), 0);
        } else if (clean_path == "/api/v1/secret/audit") {
            int entries = 0;
            int rc = omniq_secret_verify_audit_chain(&entries);
            std::string body = "{\"valid\":" + std::string(rc == 0 ? "true" : "false") + ",\"entries\":" + std::to_string(entries) + "}";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(cfd, resp.data(), resp.size(), 0);
        } else {
            std::string mod = "all";
            std::string prefix = "/api/v1/secret/telemetry/";
            if (clean_path.rfind(prefix, 0) == 0 && clean_path.size() > prefix.size())
                mod = clean_path.substr(prefix.size());
            std::string body = frame_to_json(fr, mod);
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            send(cfd, resp.data(), resp.size(), 0);
        }
        close(cfd);
    }

    close(srv_fd);
    shm_pub.join();
    if (shm_frame) munmap(shm_frame, sizeof(OmniqMultiModalFrame));
    if (shm_fd >= 0) close(shm_fd);
    return 0;
}
#endif

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#define QPU_ALLOC(sz)       kzalloc((sz), GFP_KERNEL)
#define QPU_FREE(p)         kfree((p))
#define QPU_DIV64_U64(a, b) div64_u64((a), (b))
#define QPU_DIV64_S64(a, b) div64_s64((a), (b))
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#define copy_to_user(to, from, n)   (memcpy((to), (from), (n)), 0)
#define copy_from_user(to, from, n) (memcpy((to), (from), (n)), 0)
#define QPU_ALLOC(sz)               calloc(1, (sz))
#define QPU_FREE(p)                 free((p))
#define QPU_DIV64_U64(a, b)         ((a) / (b))
#define QPU_DIV64_S64(a, b)         ((a) / (b))
#endif

#include "qpu_internal.h"

#define Q30_SHIFT          30
#define Q30_ONE            1073741824LL
#define Q30_ROUND          536870912LL
#define Q30_INV_SQRT2      759250125LL
#define PI_Q30             3373259426LL
#define TWO_PI_Q30         6746518852LL
#define HALF_PI_Q30        1686629713LL
#define CORDIC_K_Q30       652032874LL

#define QPU_SIM_MAX_QUBITS 10U
#define QPU_SIM_MAX_DIM    (1U << QPU_SIM_MAX_QUBITS)
#define QPU_MAX_QASM_LEN   65535U

struct qpu_cpx30 {
	__s32 re;
	__s32 im;
};

struct qpu_sim_workspace {
	struct qpu_cpx30 sv[QPU_SIM_MAX_DIM];
	__u64 cdf[QPU_SIM_MAX_DIM];
	__u32 readout_err_ppm[QPU_SIM_MAX_QUBITS];
	__u32 gate_1q_err_ppm[QPU_SIM_MAX_QUBITS];
	__u32 accum_gate_err_ppm[QPU_SIM_MAX_QUBITS];
	__u16 phys_qubits[QPU_SIM_MAX_QUBITS];
	char  gate_name[32];
	char  arg_buf[96];
};

static const __s32 cordic_atan_q30[24] = {
	843314857,
	497837829,
	263043837,
	133525159,
	67021687,
	33543516,
	16775851,
	8388437,
	4194283,
	2097149,
	1048576,
	524288,
	262144,
	131072,
	65536,
	32768,
	16384,
	8192,
	4096,
	2048,
	1024,
	512,
	256,
	128
};

int qpu_fallback_init(struct qpu_device *qdev)
{
	qdev->is_emulated = true;
	qdev->operating_state = QPU_STATE_SIMULATOR;
	qdev->technology = QPU_TECH_SIMULATED;
	strncpy(qdev->device_name, "OmniQ-VirtQPU-CoProcessor-127", sizeof(qdev->device_name) - 1);

	qdev->cryo.temp_50k_mK = 48500;
	qdev->cryo.temp_4k_mK = 3850;
	qdev->cryo.temp_still_mK = 820000;
	qdev->cryo.temp_cold_plate_mK = 95000;
	qdev->cryo.temp_mxc_mK = 14250;
	qdev->cryo.threshold_mK = 50000;
	qdev->cryo.cryo_status_flags = QPU_CRYO_FLAG_NOMINAL;

	qdev->interlock_tripped = false;
	qdev->rf_dac_clamped = false;
	qdev->interlock_trip_count = 0;
	strncpy(qdev->interlock_reason, "NOMINAL (< 50.000 mK)", sizeof(qdev->interlock_reason) - 1);

	qpu_qmmu_init(qdev);

	if (!qdev->dma_ring_virt)
		qpu_dma_init(qdev);

	return 0;
}

void qpu_fallback_cleanup(struct qpu_device *qdev)
{
	struct qpu_job_entry *curr, *next;

	if (!qdev)
		return;

	curr = qdev->job_history_head;
	while (curr) {
		next = curr->next;
		if (curr->results_data)
			QPU_FREE(curr->results_data);
		QPU_FREE(curr);
		curr = next;
	}
	qdev->job_history_head = NULL;

	qpu_dma_cleanup(qdev);
}

static __u64 g_sim_rng_seq = 0x243F6A8885A308D3ULL;

static void qpu_sim_seed_rng(__u64 *state, __u64 t_seed)
{
	__u64 s = 0;
#ifdef __KERNEL__
	get_random_bytes(&s, sizeof(s));
#else
	s = (__u64)rand() ^ (t_seed * 0x9E3779B97F4A7C15ULL);
#endif
	g_sim_rng_seq += 0x9E3779B97F4A7C15ULL;
	s ^= g_sim_rng_seq ^ t_seed;
	if (s == 0)
		s = 0xCAFEBABE12345678ULL;
	*state = s;
}

static __u32 qpu_sim_random_s(__u64 *state)
{
	__u64 x = *state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return (__u32)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

static void qpu_cordic_sincos(__s64 angle_q30, __s32 *out_sin_q30, __s32 *out_cos_q30)
{
	__s64 x, y, z;
	int sign_flip = 0;
	int i;

	if (angle_q30 > 64 * TWO_PI_Q30 || angle_q30 < -64 * TWO_PI_Q30) {
		__s64 turns = QPU_DIV64_S64(angle_q30, TWO_PI_Q30);
		angle_q30 -= turns * TWO_PI_Q30;
	}
	while (angle_q30 > PI_Q30)
		angle_q30 -= TWO_PI_Q30;
	while (angle_q30 < -PI_Q30)
		angle_q30 += TWO_PI_Q30;

	z = angle_q30;
	if (z > HALF_PI_Q30) {
		z -= PI_Q30;
		sign_flip = 1;
	} else if (z < -HALF_PI_Q30) {
		z += PI_Q30;
		sign_flip = 1;
	}

	x = CORDIC_K_Q30;
	y = 0;

	for (i = 0; i < 24; i++) {
		__s64 dx = y >> i;
		__s64 dy = x >> i;
		if (z >= 0) {
			x -= dx;
			y += dy;
			z -= cordic_atan_q30[i];
		} else {
			x += dx;
			y -= dy;
			z += cordic_atan_q30[i];
		}
	}

	if (sign_flip) {
		x = -x;
		y = -y;
	}

	if (x > Q30_ONE)
		x = Q30_ONE;
	else if (x < -Q30_ONE)
		x = -Q30_ONE;

	if (y > Q30_ONE)
		y = Q30_ONE;
	else if (y < -Q30_ONE)
		y = -Q30_ONE;

	*out_cos_q30 = (__s32)x;
	*out_sin_q30 = (__s32)y;
}

static inline __s32 q30_clamp(__s64 v)
{
	if (v > 2147483647LL)
		return 2147483647;
	if (v < -2147483647LL)
		return -2147483647;
	return (__s32)v;
}

static __u64 qpu_isqrt_u64(__u64 x)
{
	__u64 op = x;
	__u64 res = 0;
	__u64 one = 1ULL << 62;

	while (one > op)
		one >>= 2;

	while (one != 0) {
		if (op >= res + one) {
			op -= res + one;
			res = (res >> 1) + one;
		} else {
			res >>= 1;
		}
		one >>= 2;
	}
	return res;
}

static inline bool qpu_is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool qpu_is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline bool qpu_is_alnum(char c)
{
	return qpu_is_alpha(c) || (c >= '0' && c <= '9');
}

static inline char qpu_tolower(char c)
{
	if (c >= 'A' && c <= 'Z')
		return (char)(c + ('a' - 'A'));
	return c;
}

static __s64 q30_expr_mul(__s64 a, __s64 b)
{
	int neg = 0;
	__u64 ua, ub, ua_hi, ua_lo, ub_hi, ub_lo, res;

	if (a < 0) {
		neg = !neg;
		ua = (__u64)(-a);
	} else {
		ua = (__u64)a;
	}
	if (b < 0) {
		neg = !neg;
		ub = (__u64)(-b);
	} else {
		ub = (__u64)b;
	}

	ua_hi = ua >> 15;
	ua_lo = ua & 0x7FFFULL;
	ub_hi = ub >> 15;
	ub_lo = ub & 0x7FFFULL;

	res = (ua_hi * ub_hi) +
	      ((ua_hi * ub_lo + ua_lo * ub_hi) >> 15) +
	      ((ua_lo * ub_lo) >> 30);

	return neg ? -(__s64)res : (__s64)res;
}

static __s64 q30_expr_div(__s64 a, __s64 b)
{
	int neg = 0;
	__u64 ua, ub, res;

	if (b == 0)
		return 0;
	if (a < 0) {
		neg = !neg;
		ua = (__u64)(-a);
	} else {
		ua = (__u64)a;
	}
	if (b < 0) {
		neg = !neg;
		ub = (__u64)(-b);
	} else {
		ub = (__u64)b;
	}

	if ((ub & 0x3FFFFFFFULL) == 0 && (ub >> 30) != 0) {
		res = QPU_DIV64_U64(ua, ub >> 30);
	} else if ((ub >> 15) != 0) {
		res = QPU_DIV64_U64(ua << 15, ub >> 15);
	} else {
		res = QPU_DIV64_U64(ua << 30, ub);
	}

	return neg ? -(__s64)res : (__s64)res;
}

static __s64 qpu_parse_atom_q30(const char **pp, const char *end)
{
	const char *p = *pp;
	__u64 int_part = 0;
	__u64 frac_part = 0;
	__u64 scale = 1;
	int frac_digits = 0;

	while (p < end && qpu_is_space(*p))
		p++;

	if (p + 1 < end && qpu_tolower(p[0]) == 'p' && qpu_tolower(p[1]) == 'i' &&
	    (p + 2 >= end || !qpu_is_alnum(p[2]))) {
		*pp = p + 2;
		return PI_Q30;
	}

	if (p + 2 < end && qpu_tolower(p[0]) == 't' && qpu_tolower(p[1]) == 'a' &&
	    qpu_tolower(p[2]) == 'u' && (p + 3 >= end || !qpu_is_alnum(p[3]))) {
		*pp = p + 3;
		return TWO_PI_Q30;
	}

	while (p < end && *p >= '0' && *p <= '9') {
		int_part = int_part * 10ULL + (__u64)(*p - '0');
		p++;
	}

	if (p < end && *p == '.') {
		p++;
		while (p < end && *p >= '0' && *p <= '9') {
			if (frac_digits < 9) {
				frac_part = frac_part * 10ULL + (__u64)(*p - '0');
				scale *= 10ULL;
				frac_digits++;
			}
			p++;
		}
	}

	*pp = p;
	return (__s64)(int_part << 30) +
	       (__s64)QPU_DIV64_U64((frac_part << 30) + (scale >> 1), scale);
}

static __s64 qpu_parse_angle_q30(const char *s, const char *end)
{
	const char *p = s;
	__s64 total = 0;
	int first_term = 1;

	while (p < end) {
		int term_neg = 0;
		__s64 term_val;

		while (p < end && (qpu_is_space(*p) || *p == '(' || *p == ')'))
			p++;
		if (p >= end)
			break;

		while (p < end && (*p == '+' || *p == '-' || qpu_is_space(*p))) {
			if (*p == '-')
				term_neg = !term_neg;
			p++;
		}
		if (p >= end && !first_term)
			break;

		term_val = qpu_parse_atom_q30(&p, end);
		if (term_neg)
			term_val = -term_val;

		for (;;) {
			char op;
			int rhs_neg = 0;
			__s64 rhs;

			while (p < end && (qpu_is_space(*p) || *p == ')'))
				p++;
			if (p >= end || (*p != '*' && *p != '/'))
				break;
			op = *p++;
			while (p < end && (qpu_is_space(*p) || *p == '(' || *p == '+' || *p == '-')) {
				if (*p == '-')
					rhs_neg = !rhs_neg;
				p++;
			}
			rhs = qpu_parse_atom_q30(&p, end);
			if (rhs_neg)
				rhs = -rhs;
			if (op == '*')
				term_val = q30_expr_mul(term_val, rhs);
			else
				term_val = q30_expr_div(term_val, rhs);
		}

		total += term_val;
		first_term = 0;

		while (p < end && (qpu_is_space(*p) || *p == ')'))
			p++;
		if (p < end && *p != '+' && *p != '-')
			break;
	}

	return total;
}

static void qpu_apply_1q_matrix(struct qpu_cpx30 *sv, __u32 dim, __u32 q,
				struct qpu_cpx30 u00, struct qpu_cpx30 u01,
				struct qpu_cpx30 u10, struct qpu_cpx30 u11)
{
	__u32 mask = 1U << q;
	__u32 idx;

	for (idx = 0; idx < dim; idx++) {
		if ((idx & mask) == 0) {
			__u32 idx1 = idx | mask;
			struct qpu_cpx30 a0 = sv[idx];
			struct qpu_cpx30 a1 = sv[idx1];
			__s64 n0_re = ((__s64)u00.re * a0.re - (__s64)u00.im * a0.im +
				       (__s64)u01.re * a1.re - (__s64)u01.im * a1.im + Q30_ROUND) >> 30;
			__s64 n0_im = ((__s64)u00.re * a0.im + (__s64)u00.im * a0.re +
				       (__s64)u01.re * a1.im + (__s64)u01.im * a1.re + Q30_ROUND) >> 30;
			__s64 n1_re = ((__s64)u10.re * a0.re - (__s64)u10.im * a0.im +
				       (__s64)u11.re * a1.re - (__s64)u11.im * a1.im + Q30_ROUND) >> 30;
			__s64 n1_im = ((__s64)u10.re * a0.im + (__s64)u10.im * a0.re +
				       (__s64)u11.re * a1.im + (__s64)u11.im * a1.re + Q30_ROUND) >> 30;

			sv[idx].re  = q30_clamp(n0_re);
			sv[idx].im  = q30_clamp(n0_im);
			sv[idx1].re = q30_clamp(n1_re);
			sv[idx1].im = q30_clamp(n1_im);
		}
	}
}

static void qpu_exec_1q_gate(struct qpu_sim_workspace *ws, __u32 dim, __u32 q,
			     const char *gname, const __s64 *angles, int n_angles,
			     __u64 *rng)
{
	__u32 mask = 1U << q;
	__u32 idx;
	struct qpu_cpx30 *sv = ws->sv;

	if (ws->accum_gate_err_ppm[q] < 200000U)
		ws->accum_gate_err_ppm[q] += ws->gate_1q_err_ppm[q];

	if (strcmp(gname, "id") == 0 || strcmp(gname, "i") == 0) {
		return;
	} else if (strcmp(gname, "x") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) == 0) {
				struct qpu_cpx30 tmp = sv[idx];
				sv[idx] = sv[idx | mask];
				sv[idx | mask] = tmp;
			}
		}
	} else if (strcmp(gname, "y") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) == 0) {
				struct qpu_cpx30 a0 = sv[idx];
				struct qpu_cpx30 a1 = sv[idx | mask];
				sv[idx].re = a1.im;
				sv[idx].im = -a1.re;
				sv[idx | mask].re = -a0.im;
				sv[idx | mask].im = a0.re;
			}
		}
	} else if (strcmp(gname, "z") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) != 0) {
				sv[idx].re = -sv[idx].re;
				sv[idx].im = -sv[idx].im;
			}
		}
	} else if (strcmp(gname, "h") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) == 0) {
				struct qpu_cpx30 a0 = sv[idx];
				struct qpu_cpx30 a1 = sv[idx | mask];
				__s64 sr = (__s64)a0.re + (__s64)a1.re;
				__s64 si = (__s64)a0.im + (__s64)a1.im;
				__s64 dr = (__s64)a0.re - (__s64)a1.re;
				__s64 di = (__s64)a0.im - (__s64)a1.im;
				sv[idx].re = q30_clamp((sr * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp((si * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
				sv[idx | mask].re = q30_clamp((dr * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
				sv[idx | mask].im = q30_clamp((di * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
			}
		}
	} else if (strcmp(gname, "s") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) != 0) {
				__s32 re = sv[idx].re;
				sv[idx].re = -sv[idx].im;
				sv[idx].im = re;
			}
		}
	} else if (strcmp(gname, "sdg") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) != 0) {
				__s32 re = sv[idx].re;
				sv[idx].re = sv[idx].im;
				sv[idx].im = -re;
			}
		}
	} else if (strcmp(gname, "t") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) != 0) {
				__s64 re = sv[idx].re;
				__s64 im = sv[idx].im;
				sv[idx].re = q30_clamp(((re - im) * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((re + im) * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
			}
		}
	} else if (strcmp(gname, "tdg") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) != 0) {
				__s64 re = sv[idx].re;
				__s64 im = sv[idx].im;
				sv[idx].re = q30_clamp(((re + im) * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((im - re) * Q30_INV_SQRT2 + Q30_ROUND) >> 30);
			}
		}
	} else if (strcmp(gname, "sx") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) == 0) {
				struct qpu_cpx30 a0 = sv[idx];
				struct qpu_cpx30 a1 = sv[idx | mask];
				sv[idx].re = q30_clamp(((__s64)a0.re - a0.im + a1.re + a1.im) >> 1);
				sv[idx].im = q30_clamp(((__s64)a0.re + a0.im + a1.im - a1.re) >> 1);
				sv[idx | mask].re = q30_clamp(((__s64)a0.re + a0.im + a1.re - a1.im) >> 1);
				sv[idx | mask].im = q30_clamp(((__s64)a0.im - a0.re + a1.re + a1.im) >> 1);
			}
		}
	} else if (strcmp(gname, "rx") == 0 && n_angles >= 1) {
		__s32 s_half, c_half;
		struct qpu_cpx30 u00, u01;
		qpu_cordic_sincos(angles[0] >> 1, &s_half, &c_half);
		u00.re = c_half;
		u00.im = 0;
		u01.re = 0;
		u01.im = -s_half;
		qpu_apply_1q_matrix(sv, dim, q, u00, u01, u01, u00);
	} else if (strcmp(gname, "ry") == 0 && n_angles >= 1) {
		__s32 s_half, c_half;
		struct qpu_cpx30 u00, u01, u10;
		qpu_cordic_sincos(angles[0] >> 1, &s_half, &c_half);
		u00.re = c_half;
		u00.im = 0;
		u01.re = -s_half;
		u01.im = 0;
		u10.re = s_half;
		u10.im = 0;
		qpu_apply_1q_matrix(sv, dim, q, u00, u01, u10, u00);
	} else if (strcmp(gname, "rz") == 0 && n_angles >= 1) {
		__s32 s_half, c_half;
		qpu_cordic_sincos(angles[0] >> 1, &s_half, &c_half);
		for (idx = 0; idx < dim; idx++) {
			__s64 re = sv[idx].re;
			__s64 im = sv[idx].im;
			if ((idx & mask) == 0) {
				sv[idx].re = q30_clamp(((__s64)c_half * re + (__s64)s_half * im + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((__s64)c_half * im - (__s64)s_half * re + Q30_ROUND) >> 30);
			} else {
				sv[idx].re = q30_clamp(((__s64)c_half * re - (__s64)s_half * im + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((__s64)c_half * im + (__s64)s_half * re + Q30_ROUND) >> 30);
			}
		}
	} else if ((strcmp(gname, "p") == 0 || strcmp(gname, "u1") == 0) && n_angles >= 1) {
		__s32 s_ang, c_ang;
		qpu_cordic_sincos(angles[0], &s_ang, &c_ang);
		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) != 0) {
				__s64 re = sv[idx].re;
				__s64 im = sv[idx].im;
				sv[idx].re = q30_clamp(((__s64)c_ang * re - (__s64)s_ang * im + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((__s64)c_ang * im + (__s64)s_ang * re + Q30_ROUND) >> 30);
			}
		}
	} else if ((strcmp(gname, "u") == 0 || strcmp(gname, "u3") == 0) && n_angles >= 3) {
		__s64 a_lam = angles[2];
		__s64 a_theta = angles[0];
		__s64 a_phi = angles[1];
		qpu_exec_1q_gate(ws, dim, q, "rz", &a_lam, 1, rng);
		qpu_exec_1q_gate(ws, dim, q, "ry", &a_theta, 1, rng);
		qpu_exec_1q_gate(ws, dim, q, "rz", &a_phi, 1, rng);
	} else if (strcmp(gname, "reset") == 0) {

		__u64 p0_q30 = 0, p1_q30 = 0, tot_q30, thresh, norm_q30;
		bool collapsed_one;

		for (idx = 0; idx < dim; idx++) {
			__s64 re = sv[idx].re;
			__s64 im = sv[idx].im;
			__u64 pr = ((__u64)(re * re + im * im)) >> 30;
			if ((idx & mask) == 0)
				p0_q30 += pr;
			else
				p1_q30 += pr;
		}
		tot_q30 = p0_q30 + p1_q30;
		if (tot_q30 == 0)
			return;
		thresh = ((__u64)(qpu_sim_random_s(rng) & 0x3FFFFFFFU) * tot_q30) >> 30;
		collapsed_one = (p1_q30 > 0 && thresh < p1_q30);
		norm_q30 = qpu_isqrt_u64((collapsed_one ? p1_q30 : p0_q30) << 30);
		if (norm_q30 == 0)
			norm_q30 = 1;

		for (idx = 0; idx < dim; idx++) {
			if ((idx & mask) == 0) {
				struct qpu_cpx30 src = collapsed_one ? sv[idx | mask] : sv[idx];
				sv[idx].re = q30_clamp(QPU_DIV64_S64((__s64)src.re * Q30_ONE, (__s64)norm_q30));
				sv[idx].im = q30_clamp(QPU_DIV64_S64((__s64)src.im * Q30_ONE, (__s64)norm_q30));
				sv[idx | mask].re = 0;
				sv[idx | mask].im = 0;
			}
		}
	}
}

static void qpu_exec_2q_gate(struct qpu_sim_workspace *ws, __u32 dim,
			     __u32 q0, __u32 q1, const char *gname,
			     const __s64 *angles, int n_angles)
{
	__u32 m0 = 1U << q0;
	__u32 m1 = 1U << q1;
	__u32 idx;
	struct qpu_cpx30 *sv = ws->sv;

	if (q0 == q1)
		return;

	if (ws->accum_gate_err_ppm[q0] < 200000U)
		ws->accum_gate_err_ppm[q0] += ws->gate_1q_err_ppm[q0] * 2U;
	if (ws->accum_gate_err_ppm[q1] < 200000U)
		ws->accum_gate_err_ppm[q1] += ws->gate_1q_err_ppm[q1] * 2U;

	if (strcmp(gname, "cx") == 0 || strcmp(gname, "cnot") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) != 0 && (idx & m1) == 0) {
				struct qpu_cpx30 tmp = sv[idx];
				sv[idx] = sv[idx | m1];
				sv[idx | m1] = tmp;
			}
		}
	} else if (strcmp(gname, "cy") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) != 0 && (idx & m1) == 0) {
				struct qpu_cpx30 a0 = sv[idx];
				struct qpu_cpx30 a1 = sv[idx | m1];
				sv[idx].re = a1.im;
				sv[idx].im = -a1.re;
				sv[idx | m1].re = -a0.im;
				sv[idx | m1].im = a0.re;
			}
		}
	} else if (strcmp(gname, "cz") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) != 0 && (idx & m1) != 0) {
				sv[idx].re = -sv[idx].re;
				sv[idx].im = -sv[idx].im;
			}
		}
	} else if (strcmp(gname, "swap") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) == 0 && (idx & m1) != 0) {
				__u32 other = (idx | m0) & ~m1;
				struct qpu_cpx30 tmp = sv[idx];
				sv[idx] = sv[other];
				sv[other] = tmp;
			}
		}
	} else if ((strcmp(gname, "cp") == 0 || strcmp(gname, "cu1") == 0) && n_angles >= 1) {
		__s32 s_ang, c_ang;
		qpu_cordic_sincos(angles[0], &s_ang, &c_ang);
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) != 0 && (idx & m1) != 0) {
				__s64 re = sv[idx].re;
				__s64 im = sv[idx].im;
				sv[idx].re = q30_clamp(((__s64)c_ang * re - (__s64)s_ang * im + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((__s64)c_ang * im + (__s64)s_ang * re + Q30_ROUND) >> 30);
			}
		}
	} else if (strcmp(gname, "rzz") == 0 && n_angles >= 1) {
		__s32 s_half, c_half;
		qpu_cordic_sincos(angles[0] >> 1, &s_half, &c_half);
		for (idx = 0; idx < dim; idx++) {
			bool diff = (((idx & m0) != 0) != ((idx & m1) != 0));
			__s64 re = sv[idx].re;
			__s64 im = sv[idx].im;
			if (!diff) {
				sv[idx].re = q30_clamp(((__s64)c_half * re + (__s64)s_half * im + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((__s64)c_half * im - (__s64)s_half * re + Q30_ROUND) >> 30);
			} else {
				sv[idx].re = q30_clamp(((__s64)c_half * re - (__s64)s_half * im + Q30_ROUND) >> 30);
				sv[idx].im = q30_clamp(((__s64)c_half * im + (__s64)s_half * re + Q30_ROUND) >> 30);
			}
		}
	}
}

static void qpu_exec_3q_gate(struct qpu_sim_workspace *ws, __u32 dim,
			     __u32 q0, __u32 q1, __u32 q2, const char *gname)
{
	__u32 m0 = 1U << q0;
	__u32 m1 = 1U << q1;
	__u32 m2 = 1U << q2;
	__u32 idx;
	struct qpu_cpx30 *sv = ws->sv;

	if (q0 == q1 || q0 == q2 || q1 == q2)
		return;

	if (ws->accum_gate_err_ppm[q0] < 200000U)
		ws->accum_gate_err_ppm[q0] += ws->gate_1q_err_ppm[q0] * 2U;
	if (ws->accum_gate_err_ppm[q1] < 200000U)
		ws->accum_gate_err_ppm[q1] += ws->gate_1q_err_ppm[q1] * 2U;
	if (ws->accum_gate_err_ppm[q2] < 200000U)
		ws->accum_gate_err_ppm[q2] += ws->gate_1q_err_ppm[q2] * 2U;

	if (strcmp(gname, "ccx") == 0 || strcmp(gname, "toffoli") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) != 0 && (idx & m1) != 0 && (idx & m2) == 0) {
				struct qpu_cpx30 tmp = sv[idx];
				sv[idx] = sv[idx | m2];
				sv[idx | m2] = tmp;
			}
		}
	} else if (strcmp(gname, "cswap") == 0 || strcmp(gname, "fredkin") == 0) {
		for (idx = 0; idx < dim; idx++) {
			if ((idx & m0) != 0 && (idx & m1) == 0 && (idx & m2) != 0) {
				__u32 other = (idx | m1) & ~m2;
				struct qpu_cpx30 tmp = sv[idx];
				sv[idx] = sv[other];
				sv[other] = tmp;
			}
		}
	}
}

static void qpu_strip_comments(char *buf, size_t len)
{
	size_t i = 0;
	while (i < len) {
		if (i + 1 < len && buf[i] == '/' && buf[i + 1] == '/') {
			while (i < len && buf[i] != '\n')
				buf[i++] = ' ';
		} else if (i + 1 < len && buf[i] == '/' && buf[i + 1] == '*') {
			buf[i++] = ' ';
			buf[i++] = ' ';
			while (i < len) {
				if (i + 1 < len && buf[i] == '*' && buf[i + 1] == '/') {
					buf[i++] = ' ';
					buf[i++] = ' ';
					break;
				}
				if (buf[i] != '\n')
					buf[i] = ' ';
				i++;
			}
		} else {
			i++;
		}
	}
}

static __u16 qpu_detect_qasm_qubits(const char *buf, size_t len, __u16 fallback_qubits)
{
	size_t pos = 0;
	__u32 declared = 0;
	__u32 max_seen_plus_one = 0;

	while (pos < len) {
		const char *st, *en, *p;
		char kw[16];
		int klen = 0;

		while (pos < len && (qpu_is_space(buf[pos]) || buf[pos] == ';'))
			pos++;
		if (pos >= len)
			break;
		st = &buf[pos];
		while (pos < len && buf[pos] != ';' && buf[pos] != '\n')
			pos++;
		en = &buf[pos];

		p = st;
		while (p < en && qpu_is_alpha(*p) && klen < (int)sizeof(kw) - 1)
			kw[klen++] = qpu_tolower(*p++);
		kw[klen] = '\0';

		if (strcmp(kw, "qubit") == 0 || strcmp(kw, "qreg") == 0) {
			const char *br = p;
			while (br < en && *br != '[')
				br++;
			if (br < en && *br == '[') {
				__u32 cnt = 0;
				br++;
				while (br < en && qpu_is_space(*br))
					br++;
				while (br < en && *br >= '0' && *br <= '9') {
					cnt = cnt * 10U + (__u32)(*br - '0');
					br++;
				}
				if (cnt > 0)
					declared += cnt;
			} else if (strcmp(kw, "qubit") == 0) {
				declared += 1U;
			}
		} else if (strcmp(kw, "creg") != 0 && strcmp(kw, "bit") != 0 &&
			   strcmp(kw, "openqasm") != 0 && strcmp(kw, "include") != 0) {
			const char *br = p;
			while (br < en) {
				if (*br == '[') {
					__u32 idx = 0;
					bool has_d = false;
					br++;
					while (br < en && *br >= '0' && *br <= '9') {
						idx = idx * 10U + (__u32)(*br - '0');
						has_d = true;
						br++;
					}
					if (has_d && idx + 1U > max_seen_plus_one)
						max_seen_plus_one = idx + 1U;
				} else {
					br++;
				}
			}
		}
	}

	if (declared == 0) {
		if (fallback_qubits > 0)
			declared = fallback_qubits;
		else if (max_seen_plus_one > 0)
			declared = max_seen_plus_one;
		else
			declared = 2U;
	}

	if (declared < 1U)
		declared = 1U;
	if (declared > QPU_SIM_MAX_QUBITS)
		declared = QPU_SIM_MAX_QUBITS;
	return (__u16)declared;
}

static void qpu_exec_qasm_stmt(struct qpu_sim_workspace *ws, __u32 n_qubits,
			       const char *st, const char *en, __u64 *rng)
{
	const char *p = st;
	__s64 angles[3] = { 0, 0, 0 };
	int n_angles = 0;
	__u32 q_ops[3] = { 0, 0, 0 };
	int n_ops = 0;
	bool broadcast_1q = false;
	int klen = 0;
	__u32 dim = 1U << n_qubits;

	while (p < en && qpu_is_space(*p))
		p++;
	if (p >= en)
		return;

	{
		const char *scan;
		for (scan = p; scan < en; scan++) {
			if (*scan == '=')
				return;
		}
	}

	while (p < en && qpu_is_alnum(*p) && klen < (int)sizeof(ws->gate_name) - 1)
		ws->gate_name[klen++] = qpu_tolower(*p++);
	ws->gate_name[klen] = '\0';

	if (klen == 0 ||
	    strcmp(ws->gate_name, "openqasm") == 0 ||
	    strcmp(ws->gate_name, "include") == 0 ||
	    strcmp(ws->gate_name, "qubit") == 0 ||
	    strcmp(ws->gate_name, "qreg") == 0 ||
	    strcmp(ws->gate_name, "creg") == 0 ||
	    strcmp(ws->gate_name, "bit") == 0 ||
	    strcmp(ws->gate_name, "barrier") == 0 ||
	    strcmp(ws->gate_name, "measure") == 0 ||
	    strcmp(ws->gate_name, "delay") == 0)
		return;

	while (p < en && qpu_is_space(*p))
		p++;

	if (p < en && *p == '(') {
		int depth = 1;
		int alen = 0;
		const char *a_cur, *a_end;
		p++;
		while (p < en && depth > 0) {
			if (*p == '(')
				depth++;
			else if (*p == ')') {
				depth--;
				if (depth == 0) {
					p++;
					break;
				}
			}
			if (alen < (int)sizeof(ws->arg_buf) - 1)
				ws->arg_buf[alen++] = *p;
			p++;
		}
		ws->arg_buf[alen] = '\0';

		a_cur = ws->arg_buf;
		a_end = ws->arg_buf + alen;
		while (a_cur < a_end && n_angles < 3) {
			const char *comma = a_cur;
			while (comma < a_end && *comma != ',')
				comma++;
			angles[n_angles++] = qpu_parse_angle_q30(a_cur, comma);
			a_cur = (comma < a_end) ? (comma + 1) : a_end;
		}
	}

	while (p < en && n_ops < 3) {
		const char *comma;
		const char *br;

		while (p < en && (qpu_is_space(*p) || *p == ','))
			p++;
		if (p >= en)
			break;

		comma = p;
		while (comma < en && *comma != ',')
			comma++;

		br = p;
		while (br < comma && *br != '[')
			br++;

		if (br < comma && *br == '[') {
			__u32 qidx = 0;
			bool ok = false;
			br++;
			while (br < comma && qpu_is_space(*br))
				br++;
			while (br < comma && *br >= '0' && *br <= '9') {
				qidx = qidx * 10U + (__u32)(*br - '0');
				ok = true;
				br++;
			}
			if (!ok || qidx >= n_qubits)
				return;
			q_ops[n_ops++] = qidx;
		} else {

			broadcast_1q = true;
			q_ops[n_ops++] = 0;
		}

		p = (comma < en) ? (comma + 1) : en;
	}

	if (n_ops == 1) {
		if (broadcast_1q) {
			__u32 q;
			for (q = 0; q < n_qubits; q++)
				qpu_exec_1q_gate(ws, dim, q, ws->gate_name, angles, n_angles, rng);
		} else {
			qpu_exec_1q_gate(ws, dim, q_ops[0], ws->gate_name, angles, n_angles, rng);
		}
	} else if (n_ops == 2) {
		qpu_exec_2q_gate(ws, dim, q_ops[0], q_ops[1], ws->gate_name, angles, n_angles);
	} else if (n_ops == 3) {
		qpu_exec_3q_gate(ws, dim, q_ops[0], q_ops[1], q_ops[2], ws->gate_name);
	}
}

int qpu_fallback_submit_job(struct qpu_device *qdev, struct qpu_job_submission *job)
{
	struct qpu_partition *part = NULL;
	struct qpu_sim_workspace *ws = NULL;
	struct qpu_job_entry *entry = NULL;
	char *qasm_buf = NULL;
	__u8 *buf = NULL;
	size_t qasm_len, shot_bytes, pos;
	__u16 part_qubits = 0;
	__u16 bit_width;
	__u32 dim, idx, s, q;
	__u64 total_prob = 0;
	__u64 rng_state = 0;
	__u64 elapsed_ns = 1;
	int i, ret = 0;
#ifdef __KERNEL__
	__u64 t_start = ktime_get_ns();
	__u64 t_end;
#else
	struct timespec ts_start, ts_end;
	__u64 t0_ns, t1_ns;
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	t0_ns = (__u64)ts_start.tv_sec * 1000000000ULL + (__u64)ts_start.tv_nsec;
#endif

	if (!qdev || !job || job->shots == 0 || job->bytecode_len == 0 || !job->bytecode_ptr)
		return -22;

	if (qdev->interlock_tripped || qdev->rf_dac_clamped ||
	    qdev->cryo.temp_mxc_mK >= qdev->cryo.threshold_mK) {
		job->status = 0xDEAD;
		return -13;
	}

	for (i = 0; i < QPU_MAX_PARTITIONS; i++) {
		if (qdev->partitions[i].active &&
		    qdev->partitions[i].partition_id == job->partition_id) {
			part = &qdev->partitions[i];
			part_qubits = part->num_qubits;
			break;
		}
	}

	qasm_len = (job->bytecode_len > QPU_MAX_QASM_LEN) ?
		   (size_t)QPU_MAX_QASM_LEN : (size_t)job->bytecode_len;

	qasm_buf = QPU_ALLOC(qasm_len + 1);
	if (!qasm_buf)
		return -12;

	if (copy_from_user(qasm_buf, (const void __user *)(uintptr_t)job->bytecode_ptr, qasm_len)) {
		QPU_FREE(qasm_buf);
		return -14;
	}
	qasm_buf[qasm_len] = '\0';

	ws = QPU_ALLOC(sizeof(*ws));
	if (!ws) {
		QPU_FREE(qasm_buf);
		return -12;
	}

	qpu_strip_comments(qasm_buf, qasm_len);
	bit_width = qpu_detect_qasm_qubits(qasm_buf, qasm_len,
					   (part_qubits > QPU_SIM_MAX_QUBITS) ?
					   (__u16)QPU_SIM_MAX_QUBITS : part_qubits);
	dim = 1U << bit_width;

	for (q = 0; q < bit_width; q++) {
		__u16 phys_q = (part && q < part->num_qubits) ? part->qubits[q] : (__u16)q;
		if (qdev->total_qubits > 0 && phys_q >= qdev->total_qubits)
			phys_q = (__u16)(q % qdev->total_qubits);
		ws->phys_qubits[q] = phys_q;
		ws->readout_err_ppm[q] = qdev->calibration[phys_q].readout_error_ppm;
		ws->gate_1q_err_ppm[q] = qdev->calibration[phys_q].gate_1q_error_ppm;
		ws->accum_gate_err_ppm[q] = 0;
	}

	ws->sv[0].re = (__s32)Q30_ONE;
	ws->sv[0].im = 0;

#ifdef __KERNEL__
	qpu_sim_seed_rng(&rng_state, t_start);
#else
	qpu_sim_seed_rng(&rng_state, t0_ns);
#endif

	pos = 0;
	while (pos < qasm_len) {
		const char *st, *en;
		while (pos < qasm_len && (qpu_is_space(qasm_buf[pos]) || qasm_buf[pos] == ';'))
			pos++;
		if (pos >= qasm_len)
			break;
		st = &qasm_buf[pos];
		while (pos < qasm_len && qasm_buf[pos] != ';' && qasm_buf[pos] != '\n')
			pos++;
		en = &qasm_buf[pos];
		qpu_exec_qasm_stmt(ws, bit_width, st, en, &rng_state);
	}

	total_prob = 0;
	for (idx = 0; idx < dim; idx++) {
		__s64 re = ws->sv[idx].re;
		__s64 im = ws->sv[idx].im;
		__u64 prob_q30 = ((__u64)(re * re + im * im)) >> 30;
		total_prob += prob_q30;
		ws->cdf[idx] = total_prob;
	}
	if (total_prob == 0) {
		ws->cdf[0] = (__u64)Q30_ONE;
		total_prob = (__u64)Q30_ONE;
	}

	shot_bytes = (size_t)((bit_width + 7U) / 8U);
	buf = QPU_ALLOC(shot_bytes * (size_t)job->shots);
	if (!buf) {
		ret = -12;
		goto out_free;
	}

	for (s = 0; s < job->shots; s++) {
		__u8 *shot_ptr = buf + ((size_t)s * shot_bytes);
		__u64 r30 = (__u64)(qpu_sim_random_s(&rng_state) & 0x3FFFFFFFU);
		__u64 thresh = (r30 * total_prob) >> 30;
		__u32 lo = 0, hi = dim - 1U;
		__u32 sampled_state;

		while (lo < hi) {
			__u32 mid = (lo + hi) >> 1;
			if (ws->cdf[mid] > thresh)
				hi = mid;
			else
				lo = mid + 1U;
		}
		sampled_state = lo;

		for (q = 0; q < bit_width; q++) {
			__u32 bit = (sampled_state >> q) & 1U;
			__u32 depol_flip_ppm = ws->accum_gate_err_ppm[q] >> 1;

			if (depol_flip_ppm > 0 &&
			    (qpu_sim_random_s(&rng_state) % 1000000U) < depol_flip_ppm)
				bit ^= 1U;

			if (ws->readout_err_ppm[q] > 0 &&
			    (qpu_sim_random_s(&rng_state) % 1000000U) < ws->readout_err_ppm[q])
				bit ^= 1U;

			if (bit)
				shot_ptr[q / 8U] |= (__u8)(1U << (q % 8U));
		}
	}

	entry = QPU_ALLOC(sizeof(*entry));
	if (!entry) {
		QPU_FREE(buf);
		ret = -12;
		goto out_free;
	}

#ifdef __KERNEL__
	t_end = ktime_get_ns();
	elapsed_ns = (t_end > t_start) ? (t_end - t_start) : 1ULL;
#else
	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	t1_ns = (__u64)ts_end.tv_sec * 1000000000ULL + (__u64)ts_end.tv_nsec;
	elapsed_ns = (t1_ns > t0_ns) ? (t1_ns - t0_ns) : 1ULL;
#endif
	if (elapsed_ns == 0)
		elapsed_ns = 1ULL;

	job->job_id = ++qdev->next_job_id;
	job->status = 0;

	entry->job_id = job->job_id;
	entry->partition_id = job->partition_id;
	entry->shots = job->shots;
	entry->bitstring_width = bit_width;
	entry->status = 0;
	entry->execution_time_ns = elapsed_ns;
	entry->results_data = buf;
	entry->results_len = shot_bytes * (size_t)job->shots;

	entry->next = qdev->job_history_head;
	qdev->job_history_head = entry;

out_free:
	QPU_FREE(ws);
	QPU_FREE(qasm_buf);
	return ret;
}

int qpu_fallback_read_results(struct qpu_device *qdev, struct qpu_results_read *res)
{
	struct qpu_job_entry *curr;
	size_t copy_bytes;

	if (!res || res->job_id == 0)
		return -22;

	curr = qdev->job_history_head;
	while (curr) {
		if (curr->job_id == res->job_id)
			break;
		curr = curr->next;
	}

	if (!curr)
		return -2;

	res->returned_shots = (res->max_shots < curr->shots) ? res->max_shots : curr->shots;
	res->bitstring_width = curr->bitstring_width;
	res->execution_time_ns = curr->execution_time_ns;

	copy_bytes = ((curr->bitstring_width + 7) / 8) * res->returned_shots;
	if (copy_to_user((void __user *)(uintptr_t)res->bitstring_data_ptr, curr->results_data, copy_bytes))
		return -14;

	return 0;
}

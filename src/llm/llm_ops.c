#include "llm/llm.h"
#include "lib/t_string.h"

/* ── gather_int8: embedding lookup ── */

void
llm_gather_int8(int8_t *out, const int8_t *table, uint32_t idx, int dim)
{
    memcpy(out, table + idx * dim, dim);
}

/* ── integer square root via Newton's method ── */

static uint32_t
int_sqrt(uint32_t x)
{
    if (x <= 1) return x;
    uint32_t r = x;
    uint32_t nr;
    for (int i = 0; i < 20; i++) {
        nr = (r + x / r) / 2;
        if (nr >= r) break;
        r = nr;
    }
    return r;
}

/* ── rms_norm_int8 ──
 * Scaled-int8: input is int8 with scale x_scale (Q20), weight int8 with
 * w_scale (Q20). Output int8 + returns output scale (Q20).
 *
 * real_out[i] = (x_i8[i]*x_s)/rms_real * w_i8[i]*w_s
 * rms_real = x_s * sqrt(mean(x_i8^2)) = x_s * rms_i8  → x_s cancels!
 * so: rq[i] = x_i8[i]*w_i8[i]*w_s / rms_i8   (Q20 real)
 * then requant to int8, out_scale = max|rq| / 127.
 * Matches tools/sim_fixedpoint.py rms(). */
uint32_t
llm_rms_norm_int8(int8_t *y, const int8_t *x, const int8_t *w,
                  uint32_t w_scale, int n)
{
    int32_t sum_sq = 0;
    for (int i = 0; i < n; i++) {
        int32_t xi = (int32_t)(signed char)x[i];
        sum_sq += xi * xi;
    }
    uint32_t mean_sq = (uint32_t)sum_sq / (uint32_t)n;
    if (mean_sq == 0) mean_sq = 1;
    uint32_t rms = int_sqrt(mean_sq);
    if (rms == 0) rms = 1;

    /* rq[i] = x_i8*w_i8*w_scale / rms  (Q20 real). Find max abs. */
    static int64_t rq[4096];   /* static: avoid huge stack frame (bare-metal) */
    int64_t maxabs = 1;
    for (int i = 0; i < n; i++) {
        int64_t xi = (int64_t)(signed char)x[i];
        int64_t wi = (int64_t)(signed char)w[i];
        int64_t v = (xi * wi * (int64_t)w_scale) / (int64_t)rms;
        rq[i] = v;
        int64_t a = v < 0 ? -v : v;
        if (a > maxabs) maxabs = a;
    }
    for (int i = 0; i < n; i++) {
        int64_t num = rq[i] * 127 * 2;
        int64_t q;
        if (rq[i] >= 0) q = (num + maxabs) / (maxabs * 2);
        else            q = (num - maxabs) / (maxabs * 2);
        if (q > 127) q = 127;
        if (q < -128) q = -128;
        y[i] = (int8_t)q;
    }
    uint32_t out_s = (uint32_t)(maxabs / 127);
    if (out_s < 1) out_s = 1;
    return out_s;
}

/* ── rope_int8: rotary positional encoding with precomputed tables ── */

void
llm_rope_int8(int8_t *data, int n_heads, int dqkv, int pos,
              const int8_t *cos_table, const int8_t *sin_table, int half_d)
{
    const int8_t *cos_row = cos_table + pos * half_d;
    const int8_t *sin_row = sin_table + pos * half_d;

    for (int h = 0; h < n_heads; h++) {
        int8_t *head = data + h * dqkv;
        for (int i = 0; i < half_d; i++) {
            int32_t a = (int32_t)(signed char)head[i];
            int32_t b = (int32_t)(signed char)head[i + half_d];
            int32_t cos_val = (int32_t)(signed char)cos_row[i];
            int32_t sin_val = (int32_t)(signed char)sin_row[i];

            int32_t new_a = (a * cos_val - b * sin_val) >> 7;
            int32_t new_b = (b * cos_val + a * sin_val) >> 7;

            if (new_a > 127) new_a = 127;
            if (new_a < -128) new_a = -128;
            if (new_b > 127) new_b = 127;
            if (new_b < -128) new_b = -128;

            head[i]          = (int8_t)new_a;
            head[i + half_d] = (int8_t)new_b;
        }
    }
}

/* ── argmax ── */

uint32_t
llm_argmax(const int8_t *data, int n)
{
    int best_val = (signed char)data[0];
    uint32_t best_idx = 0;
    for (int i = 1; i < n; i++) {
        int v = (signed char)data[i];
        if (v > best_val) {
            best_val = v;
            best_idx = (uint32_t)i;
        }
    }
    return best_idx;
}

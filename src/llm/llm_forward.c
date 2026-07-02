#include "llm/llm.h"
#include "npulib/npu_math.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"
#include "dev/t_timer.h"
#include "dev/t_dw_uart.h"

/*
 * Scaled-INT8 forward pass, a direct line-by-line port of
 * tools/sim_fixedpoint.py (--int_ops 1), which is verified to produce a
 * coherent story. No floating point.
 *
 * Every activation tensor is (int8 values, scale) with real = int8 * scale / 2^20.
 *
 * IMPORTANT: on this ARM target `char`/`int8_t` is UNSIGNED. Every read of an
 * int8 activation/weight/LUT value must go through (signed char).
 */

#define SHIFT LLM_SCALE_SHIFT   /* 20 */

/* signed read of an int8 array element */
#define S8(p, i) ((int32_t)(signed char)(p)[(i)])

/* ---- small helpers, mirroring the Python ---- */

static uint32_t
isqrt_u32(uint32_t x)
{
    if (x <= 1) return x;
    uint32_t r = x, nr;
    for (int i = 0; i < 20; i++) {
        nr = (r + x / r) / 2;
        if (nr >= r) break;
        r = nr;
    }
    return r;
}

/* absmax over an int64 array (never < 1) */
static int64_t
absmax64(const int64_t *v, int n)
{
    int64_t mx = 1;
    for (int i = 0; i < n; i++) {
        int64_t a = v[i] < 0 ? -v[i] : v[i];
        if (a > mx) mx = a;
    }
    return mx;
}

/* requant real Q20 array to int8: out[i] = round(v[i]*127/mx). Python:
 *   clip((v*127*2 + sign(v)*mx)//(mx*2), -128, 127) */
static void
requant_i8(const int64_t *v, int n, int64_t mx, int8_t *out)
{
    for (int i = 0; i < n; i++) {
        int64_t num = v[i] * 127 * 2;
        int64_t q = (v[i] >= 0) ? (num + mx) / (mx * 2)
                                : (num - mx) / (mx * 2);
        if (q > 127) q = 127;
        if (q < -128) q = -128;
        out[i] = (int8_t)q;
    }
}

/* rms_norm: y (int8) = round(rq*127/mx), rq[i] = x_i8*w_i8*w_scale/rms_i8.
 * Returns output scale (Q20). Mirrors Python rms(). */
static uint32_t
rms_norm(int8_t *y, const int8_t *x, const int8_t *w, uint32_t w_scale, int n)
{
    static int64_t rq[4096];
    int64_t sum_sq = 0;
    for (int i = 0; i < n; i++) {
        int64_t xi = S8(x, i);
        sum_sq += xi * xi;
    }
    int64_t ms = sum_sq / n;
    if (ms == 0) ms = 1;
    uint32_t rms = isqrt_u32((uint32_t)ms);
    if (rms == 0) rms = 1;

    for (int i = 0; i < n; i++) {
        int64_t val = (int64_t)S8(x, i) * (int64_t)S8(w, i);   /* x_i8*w_i8 */
        rq[i] = (val * (int64_t)w_scale) / (int64_t)rms;       /* Q20 real */
    }
    int64_t mx = absmax64(rq, n);
    requant_i8(rq, n, mx, y);
    uint32_t out_s = (uint32_t)(mx / 127);
    if (out_s < 1) out_s = 1;
    return out_s;
}

/* rope in place on int8 (scale preserved). Mirrors Python rope_i8(). */
static void
rope(int8_t *data, int n_heads, int dqkv, int half_d,
     const int8_t *cos_row, const int8_t *sin_row)
{
    for (int hh = 0; hh < n_heads; hh++) {
        int8_t *head = data + hh * dqkv;
        for (int i = 0; i < half_d; i++) {
            int32_t a  = S8(head, i);
            int32_t b  = S8(head, i + half_d);
            int32_t cv = S8(cos_row, i);
            int32_t sv = S8(sin_row, i);
            int32_t na = (a * cv - b * sv) >> 7;
            int32_t nb = (b * cv + a * sv) >> 7;
            if (na > 127) na = 127; if (na < -128) na = -128;
            if (nb > 127) nb = 127; if (nb < -128) nb = -128;
            head[i]          = (int8_t)na;
            head[i + half_d] = (int8_t)nb;
        }
    }
}

/* residual add: xr = a_i8*a_s + b_i8*b_s (Q20), requant to int8, return scale */
static uint32_t
add_res(int8_t *out, const int8_t *a, uint32_t a_s,
        const int8_t *b, uint32_t b_s, int n)
{
    static int64_t xr[4096];
    for (int i = 0; i < n; i++)
        xr[i] = (int64_t)S8(a, i) * (int64_t)a_s + (int64_t)S8(b, i) * (int64_t)b_s;
    int64_t mx = absmax64(xr, n);
    requant_i8(xr, n, mx, out);
    uint32_t out_s = (uint32_t)(mx / 127);
    if (out_s < 1) out_s = 1;
    return out_s;
}

/*
 * llm_forward — one decoding step. Returns argmax token id.
 */
uint32_t
llm_forward(llm_model_t *m, uint32_t token_id, uint32_t pos)
{
    uint32_t h      = m->cfg.hidden_size;
    uint32_t n_q_h  = m->cfg.num_attention_heads;
    uint32_t n_kv_h = m->cfg.num_kv_heads;
    uint32_t dqkv   = m->dqkv;
    uint32_t half_d = dqkv / 2;
    uint32_t di     = m->cfg.intermediate_size;
    uint32_t kv_dim = n_kv_h * dqkv;
    uint32_t total_seq = pos + 1;
    uint32_t max_seq = m->cfg.max_seq_len;
    uint32_t sqrt_dqkv = isqrt_u32(dqkv);
    if (sqrt_dqkv < 1) sqrt_dqkv = 1;

    /* 1. embedding: residual = embed[token], scale = embed w_scale */
    llm_gather_int8(m->residual, m->embedding.data, token_id, (int)h);
    uint32_t res_s = m->embedding.w_scale_q20;

    for (uint32_t layer = 0; layer < m->cfg.num_hidden_layers; layer++) {

        /* 2a. RMS norm (attention) */
        uint32_t n1_s = rms_norm(m->hidden, m->residual,
                                 m->rms_att_w[layer].data,
                                 m->rms_att_w[layer].w_scale_q20, (int)h);

        /* 2b. Q/K/V projections */
        uint32_t q_s = llm_npu_matmul(m, m->q_buf, m->hidden, 1, (int)h, &m->wq[layer], n1_s, 0);
        uint32_t k_s = llm_npu_matmul(m, m->k_buf, m->hidden, 1, (int)h, &m->wk[layer], n1_s, 0);
        uint32_t v_s = llm_npu_matmul(m, m->v_buf, m->hidden, 1, (int)h, &m->wv[layer], n1_s, 0);

        /* 2c. RoPE (identity at pos 0) */
        rope(m->q_buf, (int)n_q_h, (int)dqkv, (int)half_d,
             m->cos_table + pos * half_d, m->sin_table + pos * half_d);
        rope(m->k_buf, (int)n_kv_h, (int)dqkv, (int)half_d,
             m->cos_table + pos * half_d, m->sin_table + pos * half_d);

        /* 2d. write K,V to cache with scales */
        uint32_t cache_base = layer * m->kv_stride + pos * kv_dim;
        memcpy(m->k_cache + cache_base, m->k_buf, kv_dim);
        memcpy(m->v_cache + cache_base, m->v_buf, kv_dim);
        m->k_scale[layer * max_seq + pos] = k_s;
        m->v_scale[layer * max_seq + pos] = v_s;

        /* 2e. attention (CPU). Collect real (Q20) outputs across all heads,
         * then requant together to one attn_out scale. */
        static int64_t attn_real[4096];
        static int64_t raw[512];
        static uint32_t probs[512];

        for (uint32_t qh = 0; qh < n_q_h; qh++) {
            uint32_t kvg = qh / m->n_groups;
            const int8_t *q_head = m->q_buf + qh * dqkv;

            /* raw[p] = dot(q_i8,k_i8) * q_s * k_s(p)  (Q40 real) */
            for (uint32_t p = 0; p < total_seq; p++) {
                const int8_t *k_pos = m->k_cache + layer * m->kv_stride
                                      + p * kv_dim + kvg * dqkv;
                uint32_t ks_p = m->k_scale[layer * max_seq + p];
                int32_t dot = 0;
                for (uint32_t d = 0; d < dqkv; d++)
                    dot += S8(q_head, d) * S8(k_pos, d);
                raw[p] = (int64_t)dot * (int64_t)q_s * (int64_t)ks_p;
            }

            /* softmax via exp_lut. index d = round((max_real - score_real)*32)
             * score_real = raw / 2^40 / sqrt(dqkv). probs in Q16. */
            int64_t rawmax = raw[0];
            for (uint32_t p = 1; p < total_seq; p++)
                if (raw[p] > rawmax) rawmax = raw[p];

            int64_t denom = ((int64_t)1 << (2 * SHIFT)) * (int64_t)sqrt_dqkv;
            uint64_t psum = 0;
            for (uint32_t p = 0; p < total_seq; p++) {
                /* d = round((rawmax-raw)*32 / denom) */
                int64_t d = ((rawmax - raw[p]) * 32 + denom / 2) / denom;
                if (d < 0) d = 0;
                if (d > 255) d = 255;
                uint32_t e = exp_lut[d];   /* exp_lut is uint16_t: no sign issue */
                probs[p] = e;
                psum += e;
            }
            if (psum == 0) psum = 1;
            for (uint32_t p = 0; p < total_seq; p++)
                probs[p] = (uint32_t)(((uint64_t)probs[p] * 65535) / psum);

            /* oh[d] = sum_p prob[p] * (v_i8 * v_s)  (Q16 * Q20), /65535 -> Q20 */
            for (uint32_t d = 0; d < dqkv; d++) {
                int64_t acc = 0;
                for (uint32_t p = 0; p < total_seq; p++) {
                    const int8_t *v_pos = m->v_cache + layer * m->kv_stride
                                          + p * kv_dim + kvg * dqkv;
                    uint32_t vs_p = m->v_scale[layer * max_seq + p];
                    int64_t vreal = (int64_t)S8(v_pos, d) * (int64_t)vs_p;
                    acc += (int64_t)probs[p] * vreal;
                }
                attn_real[qh * dqkv + d] = acc / 65535;
            }
        }
        int64_t ao_mx = absmax64(attn_real, (int)(n_q_h * dqkv));
        requant_i8(attn_real, (int)(n_q_h * dqkv), ao_mx, m->attn_out);
        uint32_t ao_s = (uint32_t)(ao_mx / 127);
        if (ao_s < 1) ao_s = 1;

        /* 2f. O projection + residual */
        uint32_t o_s = llm_npu_matmul(m, m->mlp_out, m->attn_out, 1, (int)h, &m->wo[layer], ao_s, 0);
        res_s = add_res(m->residual, m->residual, res_s, m->mlp_out, o_s, (int)h);

        /* 2g. MLP */
        uint32_t n2_s = rms_norm(m->hidden, m->residual,
                                 m->rms_ffn_w[layer].data,
                                 m->rms_ffn_w[layer].w_scale_q20, (int)h);
        uint32_t g_s = llm_npu_matmul(m, m->gate_buf, m->hidden, 1, (int)h, &m->w_gate[layer], n2_s, 0);
        uint32_t u_s = llm_npu_matmul(m, m->up_buf,   m->hidden, 1, (int)h, &m->w_up[layer], n2_s, 0);

        /* SwiGLU: act_real = silu(g_real) * u_real
         *   g_real = g_i8 * g_s (Q20)
         *   sig = sigmoid_lut[(g_i8*g_s*32)>>20] + 128   (0..255 ~ sigmoid*255)
         *   silu = g_real * sig / 255 (Q20)
         *   act  = silu * u_real >> 20 (Q20) */
        static int64_t act_real[4096];
        for (uint32_t i = 0; i < di; i++) {
            int32_t gi = S8(m->gate_buf, i);
            int64_t idx = ((int64_t)gi * (int64_t)g_s * 32) >> SHIFT;
            if (idx > 127) idx = 127;
            if (idx < -128) idx = -128;
            /* CRITICAL: (signed char) — int8_t is unsigned on this ARM target */
            int32_t sig = (int32_t)(signed char)sigmoid_lut[idx & 0xff] + 128;
            int64_t g_real = (int64_t)gi * (int64_t)g_s;
            int64_t silu   = (g_real * sig) / 255;
            int64_t u_real = (int64_t)S8(m->up_buf, i) * (int64_t)u_s;
            act_real[i] = (silu * u_real) >> SHIFT;
        }
        int64_t act_mx = absmax64(act_real, (int)di);
        requant_i8(act_real, (int)di, act_mx, m->gate_buf);
        uint32_t act_s = (uint32_t)(act_mx / 127);
        if (act_s < 1) act_s = 1;

        /* down projection + residual */
        uint32_t d_s = llm_npu_matmul(m, m->mlp_out, m->gate_buf, 1, (int)di, &m->w_down[layer], act_s, 0);
        res_s = add_res(m->residual, m->residual, res_s, m->mlp_out, d_s, (int)h);
    }

    /* 3. final RMS norm + LM head; argmax over raw INT32 with repetition
     * penalty (deterministic anti-loop: recently emitted tokens get their
     * logit scaled down before argmax). */
    uint32_t nf_s = rms_norm(m->hidden, m->residual,
                             m->rms_out_w.data,
                             m->rms_out_w.w_scale_q20, (int)h);
    (void)llm_npu_matmul(m, m->logits, m->hidden, 1, (int)h, &m->lm_head, nf_s,
                         m->logits32);

    uint32_t vocab = m->cfg.vocab_size;
    /* penalize recent tokens: positive logits *0.5, negative *1.5 */
    for (uint32_t r = 0; r < m->recent_n; r++) {
        uint32_t t = m->recent[r];
        if (t < vocab) {
            int32_t v = m->logits32[t];
            m->logits32[t] = (v > 0) ? v / 2 : v + (-v / 2);
        }
    }

    int32_t best = m->logits32[0];
    uint32_t result = 0;
    for (uint32_t i = 1; i < vocab; i++) {
        if (m->logits32[i] > best) { best = m->logits32[i]; result = i; }
    }

    /* record in ring history */
    uint32_t cap = (uint32_t)(sizeof(m->recent) / sizeof(m->recent[0]));
    if (m->recent_n < cap) {
        m->recent[m->recent_n++] = result;
    } else {
        for (uint32_t i = 1; i < cap; i++) m->recent[i - 1] = m->recent[i];
        m->recent[cap - 1] = result;
    }
    return result;
}

void
llm_generate(llm_model_t *m, llm_tokenizer_t *tok,
             const uint32_t *prompt_ids, int prompt_len, int max_tokens)
{
    char decode_buf[64];
    uint32_t pos = 0;

    /* Fresh story: clear the repetition-penalty history so a previous
     * generation can't bias this one. KV cache is written from pos=0 and
     * naturally overwritten as we go. */
    m->recent_n = 0;

    logger_info("LLM: generating (prompt_len=%d, max=%d)...\n", prompt_len, max_tokens);
    uint64_t start_tick = timer_get_system_ticks();

    uint32_t next_token = 0;
    for (int i = 0; i < prompt_len; i++) {
        next_token = llm_forward(m, prompt_ids[i], pos);
        pos++;
    }

    for (int step = 0; step < max_tokens; step++) {
        if (next_token == tok->eos_id) {
            logger_info("\n[EOS]\n");
            break;
        }
        llm_tokenizer_decode(tok, next_token, decode_buf, sizeof(decode_buf));
        dw_uart_putstr(decode_buf);
        next_token = llm_forward(m, next_token, pos);
        pos++;
    }

    uint64_t elapsed = timer_get_system_ticks() - start_tick;
    uint64_t ms = elapsed * 1000 / TIMER_FREQUENCY_HZ;
    logger_info("\n\nLLM: %d tokens in %llu ms (%llu ms/token)\n",
                (int)pos, ms, pos > 0 ? ms / pos : 0);
}

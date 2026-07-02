#include "yolo/yolo.h"
#include "npulib/npu_math.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"

/*
 * Integer tensor ops — exact port of tools/yolo/export_device.py::CModel and
 * sim_fixedpoint.py.  Activations are CHW int8 + Q20 scale.
 *
 * int8_t is UNSIGNED on this toolchain: read via (signed char) → S8.
 */

#define SHIFT YOLO_SCALE_SHIFT
#define S8(p, i) ((int32_t)(signed char)(p)[(i)])

/* floor division for int64 (C '/' truncates toward zero; Python '//' floors).
 * bias // x_s and concat rounding rely on floor semantics for negatives. */
static int64_t
floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* requant int64 acc → int8, return max_abs (never < 1). Mirrors _requant():
 *   clip((v*127*2 + sign(v)*mx) // (mx*2), -128, 127). */
static int64_t
requant(const int64_t *acc, int n, int8_t *out)
{
    int64_t mx = 0;
    for (int i = 0; i < n; i++) {
        int64_t a = acc[i] < 0 ? -acc[i] : acc[i];
        if (a > mx) mx = a;
    }
    if (mx < 1) mx = 1;
    int64_t d = mx * 2;
    for (int i = 0; i < n; i++) {
        int64_t num = acc[i] * 127 * 2;
        int64_t q;
        if (acc[i] >= 0) q = floordiv(num + mx, d);
        else             q = floordiv(num - mx, d);
        if (q > 127) q = 127;
        if (q < -128) q = -128;
        out[i] = (int8_t)q;
    }
    return mx;
}

/* Allocate (once) the data buffer of a result tensor. */
static int8_t *
ensure_buf(yolo_model_t *m, int idx, int c, int h, int w)
{
    yolo_tensor_t *t = &m->results[idx];
    if (!t->data)
        t->data = (int8_t *)t_mem_alloc((size_t)c * h * w);
    t->c = c; t->h = h; t->w = w;
    return t->data;
}

/*
 * conv + bias + (optional SiLU). acc32 is [oc][oh*ow] INT32 straight from the
 * NPU. x_s is input scale (Q20), ws is weight scale (Q20). Writes result tensor
 * m->results[op->out]. Uses a shared int64 scratch for the whole plane.
 */
void
yolo_conv_finish(yolo_model_t *m, const yolo_op_t *op, const int32_t *acc32,
                 int oh, int ow, uint32_t x_s)
{
    int N = op->out_c;
    int M = oh * ow;
    int total = N * M;
    static int64_t buf[409600];  /* max tensor plane (256*40*40=409600) */
    uint32_t ws = op->w_scale_q20;

    /* bias-add: acc[oc,m] + bias_q[oc] // x_s */
    for (int oc = 0; oc < N; oc++) {
        int64_t b = floordiv(op->bias_q[oc], (int64_t)x_s);
        const int32_t *arow = acc32 + (int64_t)oc * M;
        int64_t *brow = buf + (int64_t)oc * M;
        for (int i = 0; i < M; i++)
            brow[i] = (int64_t)arow[i] + b;
    }

    int8_t *out = ensure_buf(m, op->out, N, oh, ow);

    if (op->type == YOP_CONV_LIN) {
        int64_t mx = requant(buf, total, out);
        /* out_s = mx * ws * x_s / (127 * 2^20) */
        uint64_t prod = (uint64_t)mx * (uint64_t)ws * (uint64_t)x_s;
        uint64_t os = prod / ((uint64_t)127 << SHIFT);
        if (os < 1) os = 1;
        m->results[op->out].scale_q20 = (uint32_t)os;
        return;
    }

    /* YOP_CONV: requant conv acc to int8 (y), y_s, then SiLU via sigmoid_lut */
    static int8_t y_i8[409600];
    int64_t y_mx = requant(buf, total, y_i8);
    uint64_t y_s = ((uint64_t)y_mx * ws * x_s) / ((uint64_t)127 << SHIFT);
    if (y_s < 1) y_s = 1;

    /* silu_acc = y_i8 * (sigmoid_lut[idx]+128), idx = clip((y*y_s*32)>>20) */
    for (int i = 0; i < total; i++) {
        int32_t yi = (int32_t)(signed char)y_i8[i];
        int64_t idx = ((int64_t)yi * (int64_t)y_s * 32) >> SHIFT;
        if (idx > 127) idx = 127;
        if (idx < -128) idx = -128;
        int32_t sig = (int32_t)(signed char)sigmoid_lut[idx & 0xFF] + 128; /* 0..255 */
        buf[i] = (int64_t)yi * sig;
    }
    int64_t silu_mx = requant(buf, total, out);
    /* out_s = silu_mx * y_s / (127 * 255) */
    uint64_t os = ((uint64_t)silu_mx * y_s) / (127ull * 255ull);
    if (os < 1) os = 1;
    m->results[op->out].scale_q20 = (uint32_t)os;
}

/* maxpool 5x5 s1 p2 on int8 CHW (scale preserved). Pad value -128. */
void
yolo_maxpool(yolo_model_t *m, const yolo_op_t *op, const yolo_tensor_t *in)
{
    int C = in->c, H = in->h, W = in->w;
    int8_t *out = ensure_buf(m, op->out, C, H, W);
    m->results[op->out].scale_q20 = in->scale_q20;
    const int p = 2, ks = 5;
    for (int c = 0; c < C; c++) {
        const int8_t *src = in->data + (int64_t)c * H * W;
        int8_t *dst = out + (int64_t)c * H * W;
        for (int oy = 0; oy < H; oy++) {
            for (int ox = 0; ox < W; ox++) {
                int32_t mx = -128;
                for (int ky = 0; ky < ks; ky++) {
                    int iy = oy - p + ky;
                    if (iy < 0 || iy >= H) continue;
                    for (int kx = 0; kx < ks; kx++) {
                        int ix = ox - p + kx;
                        if (ix < 0 || ix >= W) continue;
                        int32_t v = S8(src, iy * W + ix);
                        if (v > mx) mx = v;
                    }
                }
                dst[oy * W + ox] = (int8_t)mx;
            }
        }
    }
}

/* nearest 2x upsample on int8 CHW (scale preserved). */
void
yolo_upsample(yolo_model_t *m, const yolo_op_t *op, const yolo_tensor_t *in)
{
    int C = in->c, H = in->h, W = in->w;
    int OH = H * 2, OW = W * 2;
    int8_t *out = ensure_buf(m, op->out, C, OH, OW);
    m->results[op->out].scale_q20 = in->scale_q20;
    for (int c = 0; c < C; c++) {
        const int8_t *src = in->data + (int64_t)c * H * W;
        int8_t *dst = out + (int64_t)c * OH * OW;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int8_t v = src[y * W + x];
                dst[(2*y) * OW + (2*x)]     = v;
                dst[(2*y) * OW + (2*x+1)]   = v;
                dst[(2*y+1) * OW + (2*x)]   = v;
                dst[(2*y+1) * OW + (2*x+1)] = v;
            }
        }
    }
}

/* channel concat with scale alignment to the LARGER scale. Mirrors concat().
 *   if a_s >= b_s: keep a, rescale b: b_r = clip((b*b_s + a_s/2)//a_s); out_s=a_s
 *   else:          keep b, rescale a similarly; out_s=b_s
 * CHW layout: output channels = a.c + b.c, spatial unchanged. */
void
yolo_concat(yolo_model_t *m, const yolo_op_t *op,
            const yolo_tensor_t *a, const yolo_tensor_t *b)
{
    int H = a->h, W = a->w;
    int plane = H * W;
    int OC = a->c + b->c;
    int8_t *out = ensure_buf(m, op->out, OC, H, W);
    uint32_t a_s = a->scale_q20, b_s = b->scale_q20;

    if (a_s >= b_s) {
        /* copy a verbatim */
        memcpy(out, a->data, (int64_t)a->c * plane);
        int8_t *dst = out + (int64_t)a->c * plane;
        int64_t half = a_s / 2;
        for (int64_t i = 0; i < (int64_t)b->c * plane; i++) {
            int64_t v = floordiv((int64_t)S8(b->data, i) * b_s + half, (int64_t)a_s);
            if (v > 127) v = 127;
            if (v < -128) v = -128;
            dst[i] = (int8_t)v;
        }
        m->results[op->out].scale_q20 = a_s;
    } else {
        int64_t half = b_s / 2;
        for (int64_t i = 0; i < (int64_t)a->c * plane; i++) {
            int64_t v = floordiv((int64_t)S8(a->data, i) * a_s + half, (int64_t)b_s);
            if (v > 127) v = 127;
            if (v < -128) v = -128;
            out[i] = (int8_t)v;
        }
        memcpy(out + (int64_t)a->c * plane, b->data, (int64_t)b->c * plane);
        m->results[op->out].scale_q20 = b_s;
    }
}

/* residual add with scale alignment + requant. Mirrors add_res():
 *   xr = a_i8*a_s + b_i8*b_s; requant; out_s = mx//127. */
void
yolo_add(yolo_model_t *m, const yolo_op_t *op,
         const yolo_tensor_t *a, const yolo_tensor_t *b)
{
    int C = a->c, H = a->h, W = a->w;
    int total = C * H * W;
    static int64_t buf[409600];
    uint32_t a_s = a->scale_q20, b_s = b->scale_q20;
    for (int i = 0; i < total; i++)
        buf[i] = (int64_t)S8(a->data, i) * a_s + (int64_t)S8(b->data, i) * b_s;
    int8_t *out = ensure_buf(m, op->out, C, H, W);
    int64_t mx = requant(buf, total, out);
    uint32_t os = (uint32_t)(mx / 127);
    if (os < 1) os = 1;
    m->results[op->out].scale_q20 = os;
}

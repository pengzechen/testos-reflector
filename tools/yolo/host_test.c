/*
 * host_test.c — validate the DEVICE yolo C sources on the host.
 *
 * Compiles the real src/yolo/{yolo_model,yolo_ops,yolo_forward,yolo_postprocess}.c
 * against tiny stubs for the bare-metal deps (mem, logger, cache, timer, NPU
 * layout helpers), and replaces the NPU conv with a CPU integer im2col+matmul
 * that produces the SAME INT32 accumulator the NPU would.  Then loads the real
 * weights/yolov5n.ydev and runs forward + postprocess.
 *
 * A match with export_device.py --sim proves the C arithmetic (bias/requant/
 * SiLU/concat/add/decode/NMS/rescale) is correct.  The NPU register generation
 * itself is already covered by the existing test_conv2d/test_tile_matmul tests.
 *
 * Build:  gcc -std=gnu11 -O0 -I../../include -I../../src host_test.c -o /tmp/yt
 */
/* Do NOT include libc stdint/stdio/stdlib headers: the project's t_types.h
 * redefines uint*_t/size_t and (crucially) makes int8_t == char (unsigned on
 * the ARM target).  We rely on that unsigned-ness via (signed char) casts, so
 * we must use the project types everywhere.  Declare the few libc funcs we use. */
#include "t_types.h"

extern int    printf(const char *, ...);
extern void  *malloc(size_t);
extern void  *calloc(size_t, size_t);
extern void   free(void *);
extern void  *memcpy(void *, const void *, size_t);
extern void  *memset(void *, int, size_t);
typedef struct _IO_FILE FILE;
extern FILE  *fopen(const char *, const char *);
extern int    fclose(FILE *);
extern long   ftell(FILE *);
extern int    fseek(FILE *, long, int);
extern size_t fread(void *, size_t, size_t, FILE *);
#define SEEK_SET 0
#define SEEK_END 2

/* ---- stubs for bare-metal deps ---- */
void *t_mem_alloc(size_t n) { return calloc(1, n); }
void  t_mem_free(void *p)   { free(p); }

int32_t clean_dcache_va_range(const void *a, unsigned long n)      { (void)a; (void)n; return 0; }
int32_t invalidate_dcache_va_range(const void *a, unsigned long n) { (void)a; (void)n; return 0; }

unsigned long long timer_get_system_ticks(void) { return 0; }
#ifndef TIMER_FREQUENCY_HZ
#define TIMER_FREQUENCY_HZ 24000000ull
#endif

/* logger → printf */
#define logger_info(...)  printf(__VA_ARGS__)
#define logger_error(...) printf(__VA_ARGS__)
#define logger_warn(...)  printf(__VA_ARGS__)

/* real memcpy_neon (yolo_ops.c concat uses it via memcpy; declared in t_string.h) */
void memcpy_neon(uint8_t *d, const uint8_t *s, size_t n) { memcpy(d, s, n); }

/* NPU layout helpers used by yolo_model.c prelayout (real formula so the
 * prelayout writes stay in-bounds; the host conv path ignores the result). */
int weight_int8(int C, int k, int c) {
    int kpg = (k - 1) / 32, cpg = (c - 1) / 32;
    return (cpg * 32) * 32 + (kpg * 32 * C) + ((c - 1) % 32) + (((k - 1) % 32) * 32);
}
int feature_data(int C, int H, int W, int C2, int c, int h, int w) {
    (void)C; int plane = (c - 1) / C2; int src = plane * H * W * C2;
    int off = (c - 1) % C2; return src + C2 * ((h - 1) * W + (w - 1)) + off;
}

/* sigmoid_lut: parsed straight from the device source at build time via -include?
 * simpler: declare extern and link a copy compiled from npu_math.c. */
extern const int8_t sigmoid_lut[256];

/* ---- pull in the device sources (they include yolo/yolo.h) ---- */
/* silence their hardware includes by predefining guards where needed. */
#define NPU_MATMUL_H          /* skip npulib/npu_matmul.h (we stub its fns) */
#define NPU_MATH_H            /* skip npulib/npu_math.h (we extern sigmoid_lut) */

#include "yolo/yolo.h"

/* device files: model, ops, forward, postprocess.  NOT yolo_conv.c (hardware);
 * we provide yolo_conv_npu below with a CPU integer matmul. */
#include "yolo/yolo_model.c"
#include "yolo/yolo_ops.c"
#include "yolo/yolo_forward.c"
#include "yolo/yolo_postprocess.c"

/* CPU replacement for the NPU conv: im2col + integer matmul → acc32[oc][oh*ow].
 * Bit-identical to what the NPU produces (plain INT8·INT8 → INT32 sum). */
int
yolo_conv_npu(yolo_model_t *m, const yolo_op_t *op,
              const yolo_tensor_t *in, int32_t *out32, int *out_h, int *out_w)
{
    (void)m;
    int C = in->c, H = in->h, W = in->w;
    int kh = op->kh, kw = op->kw, stride = op->stride, pad = op->pad;
    int oh = (H + 2 * pad - kh) / stride + 1;
    int ow = (W + 2 * pad - kw) / stride + 1;
    int N = op->out_c, K = C * kh * kw, M = oh * ow;
    *out_h = oh; *out_w = ow;

    const int8_t *w = op->weights;   /* [oc][ic][kh][kw] == [oc][K] */
    for (int oc = 0; oc < N; oc++) {
        const int8_t *wr = w + (int64_t)oc * K;
        for (int oy = 0; oy < oh; oy++) {
            for (int ox = 0; ox < ow; ox++) {
                int by = oy * stride - pad, bx = ox * stride - pad;
                int32_t acc = 0;
                for (int c = 0; c < C; c++) {
                    const int8_t *ch = in->data + (int64_t)c * H * W;
                    for (int kr = 0; kr < kh; kr++) {
                        int iy = by + kr;
                        if (iy < 0 || iy >= H) continue;
                        for (int kc = 0; kc < kw; kc++) {
                            int ix = bx + kc;
                            if (ix < 0 || ix >= W) continue;
                            int k = (c * kh + kr) * kw + kc;
                            acc += (int32_t)(signed char)ch[iy * W + ix]
                                 * (int32_t)(signed char)wr[k];
                        }
                    }
                }
                out32[(int64_t)oc * M + oy * ow + ox] = acc;
            }
        }
    }
    return 0;
}

int
main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "weights/yolov5n.ydev";
    FILE *f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) { printf("read fail\n"); return 1; }
    fclose(f);

    static yolo_model_t m;
    if (yolo_model_load(&m, buf, (uint32_t)sz) != 0) { printf("load fail\n"); return 1; }

    yolo_forward(&m);
    uint32_t conf = (uint32_t)(0.25 * 65536);
    uint32_t iou  = (uint32_t)(0.45 * 65536);
    yolo_postprocess(&m, conf, iou);
    return 0;
}

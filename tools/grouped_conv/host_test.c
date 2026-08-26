/*
 * host_test.c — validate the DEVICE grouped-conv layout on the host.
 *
 * Compiles the REAL device source src/npulib/npu_conv2d.c against tiny stubs
 * for the bare-metal / register-emit deps, then proves the grouped-conv
 * lowering is mathematically exact:
 *
 *   grouping is realised purely as a block-diagonal weight layout
 *   (grouped_conv2d_weight) fed to the board-verified gen_conv2d_int8 conv MAC.
 *
 * The test lays out the compact grouped weights [oc][icg][kh][kw] into the full
 * tiled conv weight buffer via the DEVICE grouped_conv2d_weight(), then runs a
 * plain full-C_in conv that reads input+weights back through the SAME layout
 * helpers the NPU MAC uses (conv2d_feature_data / conv2d_weight). That produces
 * the identical INT32 accumulator the NPU produces. It is compared bit-exact
 * against a direct grouped-conv golden (sum only over the in-group channels).
 *
 * A match for groups = 1 (regular), 4 (grouped), 32 (depthwise) proves the
 * block-diagonal expansion is exact, so the on-board conv MAC yields the grouped
 * result. The NPU register generation itself is already covered by the existing
 * test_conv2d / test_tile_matmul on-board tests.
 *
 * Build: gcc -std=gnu11 -O0 -Iinclude -Isrc -Isrc/npulib \
 *            tools/grouped_conv/host_test.c -o /tmp/gct
 */
#include "t_types.h"

extern int   printf(const char *, ...);
extern void *calloc(size_t, size_t);
extern void  free(void *);
extern void *memset(void *, int, size_t);

/* ---- stubs for bare-metal deps referenced by npu_conv2d.c ---- */
void *t_mem_alloc(size_t n) { return calloc(1, n); }
void  t_mem_free(void *p)   { free(p); }

/* NC1HWC2 / tiled-weight layout formulas — verbatim from src/npulib/npu_matmul.c
 * (identical copies are used in tools/yolo/host_test.c and every on-board test). */
int weight_int8(int C, int k, int c) {
    int kpg = (k - 1) / 32, cpg = (c - 1) / 32;
    return (cpg * 32) * 32 + (kpg * 32 * C) + ((c - 1) % 32) + (((k - 1) % 32) * 32);
}
int weight_fp16(int C, int k, int c) {
    int kpg = (k - 1) / 16, cpg = (c - 1) / 32;
    return (cpg * 32) * 16 + (kpg * 16 * C) + ((c - 1) % 32) + (((k - 1) % 16) * 32);
}
int feature_data(int C, int H, int W, int C2, int c, int h, int w) {
    (void)C; int plane = (c - 1) / C2; int src = plane * H * W * C2;
    int off = (c - 1) % C2; return src + C2 * ((h - 1) * W + (w - 1)) + off;
}

/* Pull in the REAL device source: gen_grouped_conv2d_int8, grouped_conv2d_weight,
 * conv2d_weight, conv2d_feature_data, gen_conv2d_int8, ... */
#include "npu_conv2d.c"

/* The register serializer. On host we only validate the weight/data LAYOUT
 * math, so this is a no-op — the descriptors it would serialize are exactly
 * those the board-verified test_conv2d already exercises. Defined after the
 * device include so the descriptor struct types are in scope. */
void gen_matmul_task(uint64_t *ops, npu_cna_desc *c, npu_core_desc *co, npu_dpu_desc *d)
{ (void)ops; (void)c; (void)co; (void)d; }

/* INT8 is unsigned char on the ARM target; force signed interpretation so the
 * host arithmetic matches the NPU's signed INT8 MAC exactly. */
#define S8(x) ((int)(signed char)(x))

/* deterministic pseudo-random int8, no libc rand dependency */
static unsigned long rng = 0x12345678u;
static int8_t next_i8(void) { rng = rng * 1103515245u + 12345u; return (int8_t)((rng >> 16) & 0xff); }

#define IN_H 8
#define IN_W 8
#define IN_C 32
#define OUT_C 32
#define KH 3
#define KW 3
#define STRIDE 1
#define PAD 1
#define OUT_H ((IN_H + 2 * PAD - KH) / STRIDE + 1)
#define OUT_W ((IN_W + 2 * PAD - KW) / STRIDE + 1)

static int
run_case(int groups)
{
    int icg = IN_C / groups;              /* input channels per group  */
    int ocg = OUT_C / groups;             /* output channels per group */

    int8_t  *input_lin   = calloc(IN_H * IN_W * IN_C, 1);           /* HWC linear source */
    int8_t  *compact_w   = calloc(OUT_C * icg * KH * KW, 1);        /* [oc][icg][kh][kw] */
    int32_t *golden      = calloc(OUT_H * OUT_W * OUT_C, sizeof(int32_t));

    int8_t  *feat        = calloc(IN_C * IN_H * IN_W, 1);           /* NC1HWC2 (C2=16)   */
    int8_t  *wt          = calloc(OUT_C * KH * KW * IN_C, 1);       /* full tiled weight */
    int32_t *acc         = calloc(OUT_H * OUT_W * OUT_C, sizeof(int32_t));

    for (int i = 0; i < IN_H * IN_W * IN_C; i++) input_lin[i] = next_i8();
    for (int i = 0; i < OUT_C * icg * KH * KW; i++) compact_w[i] = next_i8();

    /* ---- direct grouped-conv golden (sums only over the in-group channels) ---- */
    for (int oh = 0; oh < OUT_H; oh++)
      for (int ow = 0; ow < OUT_W; ow++)
        for (int oc = 0; oc < OUT_C; oc++) {
            int g = oc / ocg;
            long sum = 0;
            for (int kh = 0; kh < KH; kh++)
              for (int kw = 0; kw < KW; kw++) {
                int ih = oh * STRIDE - PAD + kh, iw = ow * STRIDE - PAD + kw;
                if (ih < 0 || ih >= IN_H || iw < 0 || iw >= IN_W) continue;
                for (int ci = 0; ci < icg; ci++) {
                    int ic = g * icg + ci;
                    int iv = S8(input_lin[ih * IN_W * IN_C + iw * IN_C + ic]);
                    int wv = S8(compact_w[((oc * icg + ci) * KH + kh) * KW + kw]);
                    sum += iv * wv;
                }
              }
            golden[oh * OUT_W * OUT_C + ow * OUT_C + oc] = (int32_t)sum;
        }

    /* ---- lay input into NC1HWC2 (C2=16) via the DEVICE helper ---- */
    for (int h = 0; h < IN_H; h++)
      for (int w = 0; w < IN_W; w++)
        for (int c = 0; c < IN_C; c++)
            feat[conv2d_feature_data(IN_C, IN_H, IN_W, 16, c + 1, h + 1, w + 1)] =
                input_lin[h * IN_W * IN_C + w * IN_C + c];

    /* ---- lay compact grouped weights into full block-diagonal tiled buffer ---- */
    for (int oc = 0; oc < OUT_C; oc++)
      for (int ci = 0; ci < icg; ci++)
        for (int kh = 0; kh < KH; kh++)
          for (int kw = 0; kw < KW; kw++) {
            int src = ((oc * icg + ci) * KH + kh) * KW + kw;
            int dst = grouped_conv2d_weight(IN_C, KH, KW, OUT_C, groups,
                                            oc + 1, ci + 1, kh, kw, 1);
            wt[dst] = compact_w[src];
          }

    /* ---- emulate the NPU: a PLAIN full-C_in conv reading input+weights back
     *      through the SAME tiled layout helpers the MAC array uses. Off-block
     *      weights are zero, so this equals the grouped result iff the layout
     *      math is exact. ---- */
    for (int oh = 0; oh < OUT_H; oh++)
      for (int ow = 0; ow < OUT_W; ow++)
        for (int oc = 0; oc < OUT_C; oc++) {
            long sum = 0;
            for (int kh = 0; kh < KH; kh++)
              for (int kw = 0; kw < KW; kw++) {
                int ih = oh * STRIDE - PAD + kh, iw = ow * STRIDE - PAD + kw;
                if (ih < 0 || ih >= IN_H || iw < 0 || iw >= IN_W) continue;
                for (int ic = 0; ic < IN_C; ic++) {
                    int iv = S8(feat[conv2d_feature_data(IN_C, IN_H, IN_W, 16, ic + 1, ih + 1, iw + 1)]);
                    int wv = S8(wt[conv2d_weight(IN_C, KH, KW, OUT_C, oc + 1, ic + 1, kh, kw, 1)]);
                    sum += iv * wv;
                }
              }
            acc[oh * OUT_W * OUT_C + ow * OUT_C + oc] = (int32_t)sum;
        }

    int mism = 0;
    for (int i = 0; i < OUT_H * OUT_W * OUT_C; i++)
        if (acc[i] != golden[i]) {
            if (mism < 6) printf("    MISMATCH idx=%d golden=%d acc=%d\n", i, golden[i], acc[i]);
            mism++;
        }

    printf("  groups=%-2d (icg=%d ocg=%d): %s (%d/%d)\n",
           groups, icg, ocg, mism ? "FAIL" : "PASS",
           OUT_H * OUT_W * OUT_C - mism, OUT_H * OUT_W * OUT_C);

    free(input_lin); free(compact_w); free(golden);
    free(feat); free(wt); free(acc);
    return mism;
}

int
main(void)
{
    printf("=== Grouped Conv2D host verification (device npu_conv2d.c) ===\n");
    printf("shape: %dx%dx%d k%dx%d s%d p%d -> %dx%dx%d\n",
           IN_H, IN_W, IN_C, KH, KW, STRIDE, PAD, OUT_H, OUT_W, OUT_C);

    /* Validate the divisibility guard in the device gen function. */
    uint64_t regs[112];
    conv2d_params_t bad = { .in_h=IN_H,.in_w=IN_W,.in_c=IN_C,.out_c=OUT_C,.groups=7,
                            .kh=KH,.kw=KW,.stride_h=1,.stride_w=1,
                            .pad_top=PAD,.pad_left=PAD,.pad_bottom=PAD,.pad_right=PAD,
                            .tasks=regs,.is_int8=1 };
    int rc_bad = gen_grouped_conv2d_int8(&bad);          /* 32 % 7 != 0 -> -10 */
    conv2d_params_t good = bad; good.groups = 4;
    int rc_good = gen_grouped_conv2d_int8(&good);        /* -> 0 */
    printf("guard: groups=7 rc=%d (expect -10), groups=4 rc=%d (expect 0): %s\n",
           rc_bad, rc_good, (rc_bad == -10 && rc_good == 0) ? "PASS" : "FAIL");

    int fails = 0;
    fails += run_case(1);    /* regular conv  */
    fails += run_case(2);
    fails += run_case(4);    /* grouped conv  */
    fails += run_case(8);
    fails += run_case(16);
    fails += run_case(32);   /* depthwise     */
    fails += (rc_bad != -10 || rc_good != 0);

    printf(fails ? "RESULT: FAIL (%d)\n" : "RESULT: ALL PASS\n", fails);
    return fails ? 1 : 0;
}

/*
 * test_deconv.c — ConvTranspose / Deconvolution NPU hardware test for RK3588
 *
 * Deconv is lowered to a stride-1 conv over a dilated input with a
 * 180-flipped, in/out-channel-swapped kernel (see npu_deconv.{c,h}). Because
 * that lowering is mathematically exact, this is a REAL on-NPU test: the INT32
 * NPU output is compared bit-exact against the CPU scatter-add golden
 * (deconv_int8_ref).
 *
 * Shape: [Cin,Hin,Win] --(k,stride,pad)--> [Cout,Hout,Wout]
 *   stride=2, pad=1, k=3  => classic 2x upsample deconv (4x4 -> 7x7).
 */

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "mem/t_mem.h"
#include "npulib/npu_conv2d.h"
#include "npulib/npu_deconv.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"
#include "dev/t_timer.h"
#include "mem/cache.h"

#define IN_H   4
#define IN_W   4
#define IN_C   32
#define OUT_C  32
#define KH     3
#define KW     3
#define STRIDE 2
#define PAD    1
#define OUTPAD 0

#define DIL_H  ((IN_H - 1) * STRIDE + 1)
#define DIL_W  ((IN_W - 1) * STRIDE + 1)
#define OUT_H  ((IN_H - 1) * STRIDE - 2 * PAD + KH + OUTPAD)
#define OUT_W  ((IN_W - 1) * STRIDE - 2 * PAD + KW + OUTPAD)

/*
 * The conv engine's surf_stride register uses integer /4 and is only valid for
 * feature heights that are multiples of 4 (see npu_deconv.h). The dilated cube
 * (DIL_H x DIL_W = 7x7 here) is therefore padded up to a multiple of 4; the HW
 * runs a stride-1 conv over the padded cube and we read only the valid
 * [0,OUT_H) x [0,OUT_W) subregion of its (larger) output. Must mirror
 * deconv_conv_in_h/w and deconv_conv_out_h/w in npu_deconv.h.
 */
#define ALIGN4(x)   (((x) + 3) & ~3)
#define CPAD_TOP    (KH - 1 - PAD)
#define CPAD_BOTTOM (KH - 1 - PAD + OUTPAD)
#define CPAD_LEFT   (KW - 1 - PAD)
#define CPAD_RIGHT  (KW - 1 - PAD + OUTPAD)
#define CONV_IN_H   ALIGN4(DIL_H)
#define CONV_IN_W   ALIGN4(DIL_W)
#define CONV_OUT_H  (CONV_IN_H + CPAD_TOP + CPAD_BOTTOM - KH + 1)
#define CONV_OUT_W  (CONV_IN_W + CPAD_LEFT + CPAD_RIGHT - KW + 1)

static int8_t  input_data[IN_C * IN_H * IN_W];       /* ConvTranspose input  [Cin][Hin][Win]  */
static int8_t  weight_data[IN_C * OUT_C * KH * KW];  /* ConvTranspose weight [Cin][Cout][kh][kw] */
static int8_t  dilated_dense[IN_C * DIL_H * DIL_W];  /* dilated dense input  [Cin][Hd][Wd]    */
static int32_t expected_output[OUT_C * OUT_H * OUT_W];
static uint64_t npu_regs[112];

uint32_t rknpu_get_dma_addr(void *addr);

static int8_t
rand_int8(void)
{
    int8_t val = ((rand_tick() % 255) - 128);
    if (val > 127) val -= 128;
    if (val < -128) val += 128;
    return val;
}

void
rknpu_test_deconv(void)
{
    logger_info("=== Deconv/ConvTranspose NPU Test ===\n");
    logger_info("Input: %dx%dx%d, Kernel: %dx%d, Stride: %d, Pad: %d, OutPad: %d\n",
                IN_H, IN_W, IN_C, KH, KW, STRIDE, PAD, OUTPAD);
    logger_info("Dilated: %dx%d, Output: %dx%dx%d\n", DIL_H, DIL_W, OUT_H, OUT_W, OUT_C);

    void *regcmd  = t_mem_alloc(1024);
    npu_task_t *tasks = t_mem_alloc(1024);
    void *input   = t_mem_alloc(IN_C * CONV_IN_H * CONV_IN_W * sizeof(int8_t));
    void *weights = t_mem_alloc(OUT_C * KH * KW * IN_C * sizeof(int8_t));
    void *output  = t_mem_alloc(OUT_C * CONV_OUT_H * CONV_OUT_W * sizeof(int32_t));

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("Deconv Test: Memory allocation failed\n");
        return;
    }

    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    /* Random ConvTranspose input + weights. */
    srand_tick();
    for (int i = 0; i < IN_C * IN_H * IN_W; i++)
        input_data[i] = rand_int8();
    for (int i = 0; i < IN_C * OUT_C * KH * KW; i++)
        weight_data[i] = rand_int8();

    logger_info("Computing CPU reference (scatter-add)...\n");
    deconv_int8_ref(input_data, weight_data, expected_output,
                    IN_C, IN_H, IN_W, OUT_C,
                    KH, KW, STRIDE, STRIDE,
                    PAD, PAD, PAD, PAD, OUTPAD, OUTPAD);

    /* Build the dilated dense input (insert STRIDE-1 zeros between elements). */
    memset(dilated_dense, 0, sizeof(dilated_dense));
    for (int c = 0; c < IN_C; c++)
        for (int hi = 0; hi < IN_H; hi++)
            for (int wi = 0; wi < IN_W; wi++)
                dilated_dense[(c * DIL_H + hi * STRIDE) * DIL_W + wi * STRIDE] =
                    input_data[(c * IN_H + hi) * IN_W + wi];

    /* Pack dilated input into NC1HWC2 (C2=16 for INT8). The cube is sized to the
     * align-4-padded dims (CONV_IN_H x CONV_IN_W); dilated content fills the
     * top-left DIL_H x DIL_W, the rest stays zero (from the memset below). */
    int8_t *feat = (int8_t *)input;
    memset(feat, 0, IN_C * CONV_IN_H * CONV_IN_W);
    for (int c = 0; c < IN_C; c++)
        for (int hd = 0; hd < DIL_H; hd++)
            for (int wd = 0; wd < DIL_W; wd++) {
                int dst = conv2d_feature_data(IN_C, CONV_IN_H, CONV_IN_W, 16, c + 1, hd + 1, wd + 1);
                feat[dst] = dilated_dense[(c * DIL_H + hd) * DIL_W + wd];
            }

    /* Lay out flipped + channel-swapped weights in tiled conv format. */
    int8_t *wt = (int8_t *)weights;
    memset(wt, 0, OUT_C * KH * KW * IN_C);
    for (int ci = 0; ci < IN_C; ci++)
        for (int co = 0; co < OUT_C; co++)
            for (int ki = 0; ki < KH; ki++)
                for (int kj = 0; kj < KW; kj++) {
                    int src = ((ci * OUT_C + co) * KH + ki) * KW + kj;
                    int dst = deconv_weight(IN_C, KH, KW, OUT_C, co + 1, ci + 1, ki, kj, 1);
                    wt[dst] = weight_data[src];
                }

    deconv_params_t params = {
        .in_h = IN_H, .in_w = IN_W, .in_c = IN_C, .out_c = OUT_C,
        .kh = KH, .kw = KW,
        .stride_h = STRIDE, .stride_w = STRIDE,
        .pad_top = PAD, .pad_left = PAD, .pad_bottom = PAD, .pad_right = PAD,
        .output_padding_h = OUTPAD, .output_padding_w = OUTPAD,
        .input_dma = input_dma,
        .weights_dma = weights_dma,
        .output_dma = output_dma,
        .tasks = npu_regs,
        .is_int8 = 1,
        .out_int8 = 0,
    };

    if (gen_deconv_int8(&params) != 0) {
        logger_error("Deconv Test: gen_deconv_int8 failed\n");
        return;
    }

    memcpy(regcmd, npu_regs, sizeof(npu_regs));

    npu_task_t task = {
        .flags         = 0,
        .op_idx        = 0,
        .enable_mask   = 0xd,
        .int_mask      = 0x300,
        .int_clear     = INT_CLEAR_VALUE,
        .int_status    = 0,
        .regcfg_amount = sizeof(npu_regs) / sizeof(uint64_t) - (RKNPU_PC_DATA_EXTRA_AMOUNT + 4),
        .regcfg_offset = 0,
        .regcmd_addr   = (uint64_t)regcmd,
    };
    memcpy(&tasks[0], &task, sizeof(npu_task_t));

    memset(output, 0, OUT_C * CONV_OUT_H * CONV_OUT_W * sizeof(int32_t));

    npu_submit_t submit = {
        .flags           = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG,
        .timeout         = 5000,
        .task_start      = 0,
        .task_number     = 1,
        .task_counter    = 0,
        .priority        = 0,
        .task_obj_addr   = (uint64_t)tasks,
        .regcfg_obj_addr = 0,
        .task_base_addr  = 0,
        .user_data       = 0,
        .core_mask       = 0x1,
        .fence_fd        = -1,
    };
    submit.subcore_task[0] = (npu_subcore_task_t){.task_start = 0, .task_number = 1};
    submit.subcore_task[1] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
    submit.subcore_task[2] = (npu_subcore_task_t){.task_start = 2, .task_number = 0};
    submit.subcore_task[3] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};
    submit.subcore_task[4] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};

    clean_dcache_va_range(regcmd, 1024 * 1024 * 16);

    logger_info("Submitting deconv task to NPU...\n");
    rknpu_submit_task(&submit);

    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);
    logger_info("NPU deconv completed.\n");

    /* Verify: NPU INT32 output (NC1HWC2 C2=4) vs golden. The NPU output cube is
     * CONV_OUT_H x CONV_OUT_W (padded); the valid deconv result is its top-left
     * OUT_H x OUT_W subregion, so read with the padded stride. */
    int mismatches = 0;
    int32_t *out = (int32_t *)output;
    for (int oc = 0; oc < OUT_C; oc++)
        for (int oh = 0; oh < OUT_H; oh++)
            for (int ow = 0; ow < OUT_W; ow++) {
                int out_idx = conv2d_feature_data(OUT_C, CONV_OUT_H, CONV_OUT_W, 4, oc + 1, oh + 1, ow + 1);
                int32_t actual   = out[out_idx];
                int32_t expected = expected_output[(oc * OUT_H + oh) * OUT_W + ow];
                if (actual != expected) {
                    if (mismatches < 10)
                        logger_info("MISMATCH oc=%d oh=%d ow=%d: expected=%d actual=%d\n",
                                    oc, oh, ow, expected, actual);
                    mismatches++;
                }
            }

    if (mismatches == 0)
        logger_info("Deconv %dx%dx%d k%dx%d s%d -> %dx%dx%d: ALL PASSED\n",
                    IN_H, IN_W, IN_C, KH, KW, STRIDE, OUT_H, OUT_W, OUT_C);
    else
        logger_error("Deconv: %d mismatches out of %d\n",
                     mismatches, OUT_C * OUT_H * OUT_W);
}

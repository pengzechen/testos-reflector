/*
 * test_conv2d_bs.c — Conv2D + OUT_CVT INT8 requantization test
 *
 * Tests conv2d with out_int8=1 so the DPU applies OUT_CVT to produce
 * int8 output directly instead of raw int32 accumulator values.
 *
 * OUT_CVT formula: out = saturate_int8((conv_sum + offset) * scale >> shift)
 */

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "mem/t_mem.h"
#include "npulib/npu_conv2d.h"
#include "npulib/npu_matmul.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "dev/t_timer.h"
#include "mem/cache.h"

#define IN_H  8
#define IN_W  8
#define IN_C  32
#define OUT_C 32
#define KH    3
#define KW    3
#define STRIDE_H 1
#define STRIDE_W 1
#define PAD_TOP    1
#define PAD_LEFT   1
#define PAD_BOTTOM 1
#define PAD_RIGHT  1

#define OUT_H ((IN_H + PAD_TOP + PAD_BOTTOM - KH) / STRIDE_H + 1)
#define OUT_W ((IN_W + PAD_LEFT + PAD_RIGHT - KW) / STRIDE_W + 1)

static int8_t  input_data[IN_H * IN_W * IN_C];
static int8_t  weight_data[OUT_C * KH * KW * IN_C];
static int8_t  expected_output[OUT_H * OUT_W * OUT_C];
static uint64_t npu_regs[112];

uint32_t rknpu_get_dma_addr(void *addr);

static void
conv2d_cpu_reference_int8(int32_t cvt_offset, uint16_t cvt_scale, uint8_t cvt_shift)
{
    for (int oh = 0; oh < OUT_H; oh++) {
        for (int ow = 0; ow < OUT_W; ow++) {
            for (int oc = 0; oc < OUT_C; oc++) {
                int32_t sum = 0;
                for (int kh = 0; kh < KH; kh++) {
                    for (int kw = 0; kw < KW; kw++) {
                        int ih = oh * STRIDE_H - PAD_TOP + kh;
                        int iw = ow * STRIDE_W - PAD_LEFT + kw;
                        if (ih < 0 || ih >= IN_H || iw < 0 || iw >= IN_W)
                            continue;
                        for (int ic = 0; ic < IN_C; ic++) {
                            int8_t iv = input_data[ih * IN_W * IN_C + iw * IN_C + ic];
                            int8_t wv = weight_data[oc * KH * KW * IN_C + kh * KW * IN_C + kw * IN_C + ic];
                            sum += (int32_t)iv * (int32_t)wv;
                        }
                    }
                }
                int32_t val = (sum + cvt_offset) * cvt_scale;
                val = val >> cvt_shift;
                if (val > 127) val = 127;
                if (val < -128) val = -128;
                expected_output[oh * OUT_W * OUT_C + ow * OUT_C + oc] = (int8_t)val;
            }
        }
    }
}

static int
run_conv2d_bs_test(void *regcmd, npu_task_t *tasks, void *input, void *weights,
                   void *output, int32_t cvt_offset, uint16_t cvt_scale, uint8_t cvt_shift)
{
    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    conv2d_params_t params = {
        .in_h = IN_H, .in_w = IN_W, .in_c = IN_C,
        .out_c = OUT_C,
        .kh = KH, .kw = KW,
        .stride_h = STRIDE_H, .stride_w = STRIDE_W,
        .pad_top = PAD_TOP, .pad_left = PAD_LEFT,
        .pad_bottom = PAD_BOTTOM, .pad_right = PAD_RIGHT,
        .input_dma = input_dma,
        .weights_dma = weights_dma,
        .output_dma = output_dma,
        .tasks = npu_regs,
        .is_int8 = 1,
        .fp32tofp16 = 0,
        .out_int8 = 1,
        .cvt_offset = cvt_offset,
        .cvt_scale = cvt_scale,
        .cvt_shift = cvt_shift,
    };

    if (gen_conv2d_int8(&params) != 0) {
        logger_error("gen_conv2d_int8 failed\n");
        return -1;
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

    memset(output, 0, OUT_H * OUT_W * OUT_C * sizeof(int8_t));

    /* Layout input to NC1HWC2 (C2=16 for INT8) */
    int8_t *feat = (int8_t *)input;
    memset(feat, 0, IN_H * IN_W * IN_C);
    for (int h = 0; h < IN_H; h++)
        for (int w = 0; w < IN_W; w++)
            for (int c = 0; c < IN_C; c++)
                feat[conv2d_feature_data(IN_C, IN_H, IN_W, 16, c+1, h+1, w+1)] =
                    input_data[h * IN_W * IN_C + w * IN_C + c];

    /* Layout weights */
    int8_t *wt = (int8_t *)weights;
    memset(wt, 0, OUT_C * KH * KW * IN_C);
    for (int oc = 0; oc < OUT_C; oc++)
        for (int kh = 0; kh < KH; kh++)
            for (int kw = 0; kw < KW; kw++)
                for (int ic = 0; ic < IN_C; ic++)
                    wt[conv2d_weight(IN_C, KH, KW, OUT_C, oc+1, ic+1, kh, kw, 1)] =
                        weight_data[oc * KH * KW * IN_C + kh * KW * IN_C + kw * IN_C + ic];

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
    rknpu_submit_task(&submit);
    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);

    /* Verify INT8 output in NC1HWC2 with C2=16 */
    int mismatches = 0;
    int8_t *out = (int8_t *)output;
    for (int oh = 0; oh < OUT_H; oh++) {
        for (int ow = 0; ow < OUT_W; ow++) {
            for (int oc = 0; oc < OUT_C; oc++) {
                int out_idx = conv2d_feature_data(OUT_C, OUT_H, OUT_W, 16, oc+1, oh+1, ow+1);
                int8_t actual   = out[out_idx];
                int8_t expected = expected_output[oh * OUT_W * OUT_C + ow * OUT_C + oc];
                if (actual != expected) {
                    if (mismatches < 5)
                        logger_info("  MISMATCH oh=%d ow=%d oc=%d: expected=%d actual=%d\n",
                                    oh, ow, oc, (int)expected, (int)actual);
                    mismatches++;
                }
            }
        }
    }
    return mismatches;
}

void
rknpu_test_conv2d_bs(void)
{
    logger_info("=== Conv2D + OUT_CVT INT8 Test ===\n");
    logger_info("Input: %dx%dx%d, Kernel: %dx%d, Output: %dx%dx%d\n",
                IN_H, IN_W, IN_C, KH, KW, OUT_H, OUT_W, OUT_C);

    void *regcmd  = t_mem_alloc(1024);
    npu_task_t *tasks = t_mem_alloc(1024);
    void *input   = t_mem_alloc(IN_H * IN_W * IN_C * sizeof(int8_t));
    void *weights = t_mem_alloc(OUT_C * KH * KW * IN_C * sizeof(int8_t));
    void *output  = t_mem_alloc(OUT_H * OUT_W * OUT_C * sizeof(int8_t));

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("Conv2D BS Test: Memory allocation failed\n");
        return;
    }

    int total_pass = 1;
    int mismatches;

    /*
     * Test 1: identity CVT (offset=0, scale=1, shift=0)
     * Input=1 everywhere, weight=1 at center per channel → conv sum=1
     * Expected output: 1 for all elements
     */
    logger_info("[Test 1] Identity CVT: offset=0 scale=1 shift=0\n");
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = 1;
    memset(weight_data, 0, sizeof(weight_data));
    for (int oc = 0; oc < OUT_C; oc++) {
        int ic = oc % IN_C;
        weight_data[oc * KH * KW * IN_C + 1 * KW * IN_C + 1 * IN_C + ic] = 1;
    }
    conv2d_cpu_reference_int8(0, 1, 0);
    mismatches = run_conv2d_bs_test(regcmd, tasks, input, weights, output, 0, 1, 0);
    if (mismatches == 0)
        logger_info("  PASSED (%d elements)\n", OUT_H * OUT_W * OUT_C);
    else {
        logger_error("  FAILED: %d mismatches\n", mismatches);
        total_pass = 0;
    }

    /*
     * Test 2: shift (offset=0, scale=1, shift=2 → divides sum by 4)
     * Input=2, weight=4 at center → conv sum=8, after >>2 = 2
     */
    logger_info("[Test 2] Shift: offset=0 scale=1 shift=2 (sum=8 -> 2)\n");
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = 2;
    memset(weight_data, 0, sizeof(weight_data));
    for (int oc = 0; oc < OUT_C; oc++) {
        int ic = oc % IN_C;
        weight_data[oc * KH * KW * IN_C + 1 * KW * IN_C + 1 * IN_C + ic] = 4;
    }
    conv2d_cpu_reference_int8(0, 1, 2);
    mismatches = run_conv2d_bs_test(regcmd, tasks, input, weights, output, 0, 1, 2);
    if (mismatches == 0)
        logger_info("  PASSED (%d elements)\n", OUT_H * OUT_W * OUT_C);
    else {
        logger_error("  FAILED: %d mismatches\n", mismatches);
        total_pass = 0;
    }

    /*
     * Test 3: offset (offset=10, scale=1, shift=0)
     * Input=1, weight=2 at center → conv sum=2, after +10 = 12
     */
    logger_info("[Test 3] Offset: offset=10 scale=1 shift=0 (sum=2 -> 12)\n");
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = 1;
    memset(weight_data, 0, sizeof(weight_data));
    for (int oc = 0; oc < OUT_C; oc++) {
        int ic = oc % IN_C;
        weight_data[oc * KH * KW * IN_C + 1 * KW * IN_C + 1 * IN_C + ic] = 2;
    }
    conv2d_cpu_reference_int8(10, 1, 0);
    mismatches = run_conv2d_bs_test(regcmd, tasks, input, weights, output, 10, 1, 0);
    if (mismatches == 0)
        logger_info("  PASSED (%d elements)\n", OUT_H * OUT_W * OUT_C);
    else {
        logger_error("  FAILED: %d mismatches\n", mismatches);
        total_pass = 0;
    }

    /*
     * Test 4: saturation (large sums clamp to 127)
     * Input=3, weight=2 for ALL 32 input channels → sum=3*2*32=192, saturates to 127
     */
    logger_info("[Test 4] Saturation: sum=192 -> clamp 127\n");
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = 3;
    memset(weight_data, 0, sizeof(weight_data));
    for (int oc = 0; oc < OUT_C; oc++) {
        for (int ic = 0; ic < IN_C; ic++)
            weight_data[oc * KH * KW * IN_C + 1 * KW * IN_C + 1 * IN_C + ic] = 2;
    }
    conv2d_cpu_reference_int8(0, 1, 0);
    mismatches = run_conv2d_bs_test(regcmd, tasks, input, weights, output, 0, 1, 0);
    if (mismatches == 0)
        logger_info("  PASSED (%d elements)\n", OUT_H * OUT_W * OUT_C);
    else {
        logger_error("  FAILED: %d mismatches\n", mismatches);
        total_pass = 0;
    }

    /*
     * Test 5: negative saturation via offset
     * Input=1, weight=1 → sum=1, (1 + (-200))*1>>0 = -199, saturates to -128
     * (Avoids negative int8 input — ARM char is unsigned by default)
     */
    logger_info("[Test 5] Negative saturation via offset: (1-200) -> clamp -128\n");
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = 1;
    memset(weight_data, 0, sizeof(weight_data));
    for (int oc = 0; oc < OUT_C; oc++) {
        int ic = oc % IN_C;
        weight_data[oc * KH * KW * IN_C + 1 * KW * IN_C + 1 * IN_C + ic] = 1;
    }
    conv2d_cpu_reference_int8(-200, 1, 0);
    mismatches = run_conv2d_bs_test(regcmd, tasks, input, weights, output, -200, 1, 0);
    if (mismatches == 0)
        logger_info("  PASSED (%d elements)\n", OUT_H * OUT_W * OUT_C);
    else {
        logger_error("  FAILED: %d mismatches\n", mismatches);
        total_pass = 0;
    }

    /*
     * Test 6: scale (offset=0, scale=3, shift=1)
     * Input=1, weight=2 → sum=2, (2+0)*3>>1 = 6>>1 = 3
     */
    logger_info("[Test 6] Scale: offset=0 scale=3 shift=1 (sum=2 -> 3)\n");
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = 1;
    memset(weight_data, 0, sizeof(weight_data));
    for (int oc = 0; oc < OUT_C; oc++) {
        int ic = oc % IN_C;
        weight_data[oc * KH * KW * IN_C + 1 * KW * IN_C + 1 * IN_C + ic] = 2;
    }
    conv2d_cpu_reference_int8(0, 3, 1);
    mismatches = run_conv2d_bs_test(regcmd, tasks, input, weights, output, 0, 3, 1);
    if (mismatches == 0)
        logger_info("  PASSED (%d elements)\n", OUT_H * OUT_W * OUT_C);
    else {
        logger_error("  FAILED: %d mismatches\n", mismatches);
        total_pass = 0;
    }

    if (total_pass)
        logger_info("Conv2D+CVT INT8: ALL 6 TESTS PASSED\n");
    else
        logger_error("Conv2D+CVT INT8: SOME TESTS FAILED\n");
}

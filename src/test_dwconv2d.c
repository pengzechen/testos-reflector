/*
 * test_dwconv2d.c — Depthwise Conv2D NPU hardware test for RK3588
 *
 * Tests INT8 depthwise separable convolution on the NPU.
 * Each output channel only convolves with 1 input channel (group_size=1).
 */

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "mem/t_mem.h"
#include "npulib/npu_conv2d.h"
#include "npulib/npu_matmul.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"
#include "dev/t_timer.h"
#include "mem/cache.h"

#define IN_H  8
#define IN_W  8
#define IN_C  32
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
#define OUT_C IN_C

static int8_t  input_data[IN_H * IN_W * IN_C];
static int8_t  weight_data[IN_C * KH * KW];
static int32_t expected_output[OUT_H * OUT_W * OUT_C];
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

static void
dwconv2d_cpu_reference(void)
{
    for (int oh = 0; oh < OUT_H; oh++) {
        for (int ow = 0; ow < OUT_W; ow++) {
            for (int c = 0; c < IN_C; c++) {
                int64_t sum = 0;
                for (int kh = 0; kh < KH; kh++) {
                    for (int kw = 0; kw < KW; kw++) {
                        int ih = oh * STRIDE_H - PAD_TOP + kh;
                        int iw = ow * STRIDE_W - PAD_LEFT + kw;
                        if (ih < 0 || ih >= IN_H || iw < 0 || iw >= IN_W)
                            continue;
                        int8_t input_val  = input_data[ih * IN_W * IN_C + iw * IN_C + c];
                        int8_t weight_val = weight_data[c * KH * KW + kh * KW + kw];
                        sum += (int32_t)(input_val * weight_val);
                    }
                }
                expected_output[oh * OUT_W * OUT_C + ow * OUT_C + c] = (int32_t)sum;
            }
        }
    }
}

void
rknpu_test_dwconv2d(void)
{
    logger_info("=== Depthwise Conv2D NPU Test ===\n");
    logger_info("Input: %dx%dx%d, Kernel: %dx%d, Stride: %dx%d, Pad: %d/%d/%d/%d\n",
                IN_H, IN_W, IN_C, KH, KW, STRIDE_H, STRIDE_W,
                PAD_TOP, PAD_LEFT, PAD_BOTTOM, PAD_RIGHT);
    logger_info("Output: %dx%dx%d (depthwise, groups=%d)\n", OUT_H, OUT_W, OUT_C, IN_C);

    void *regcmd  = t_mem_alloc(1024);
    npu_task_t *tasks = t_mem_alloc(1024);
    void *input   = t_mem_alloc(IN_H * IN_W * IN_C * sizeof(int8_t));
    /* Weight buffer must be big enough for normal conv format: OUT_C * KH * KW * IN_C */
    void *weights = t_mem_alloc(OUT_C * KH * KW * IN_C * sizeof(int8_t));
    void *output  = t_mem_alloc(OUT_H * OUT_W * OUT_C * sizeof(int32_t));

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("DWConv2D Test: Memory allocation failed\n");
        return;
    }

    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    conv2d_params_t params = {
        .in_h = IN_H, .in_w = IN_W, .in_c = IN_C,
        .out_c = IN_C,
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
        .activation = ACTIVATION_NONE,
    };

    if (gen_dwconv2d_int8(&params) != 0) {
        logger_error("DWConv2D Test: gen_dwconv2d_int8 failed\n");
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

    memset(input, 0, IN_H * IN_W * IN_C);
    memset(weights, 0, OUT_C * KH * KW * IN_C);
    memset(output, 0, OUT_H * OUT_W * OUT_C * sizeof(int32_t));

    srand_tick();
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = rand_int8();
    for (int i = 0; i < IN_C * KH * KW; i++)
        weight_data[i] = rand_int8();

    logger_info("Computing CPU reference (depthwise)...\n");
    dwconv2d_cpu_reference();

    /* Layout input to NC1HWC2 format (C2=16 for INT8) */
    int8_t *feat = (int8_t *)input;
    for (int h = 0; h < IN_H; h++) {
        for (int w = 0; w < IN_W; w++) {
            for (int c = 0; c < IN_C; c++) {
                int src_idx = h * IN_W * IN_C + w * IN_C + c;
                int dst_idx = conv2d_feature_data(IN_C, IN_H, IN_W, 16, c+1, h+1, w+1);
                feat[dst_idx] = input_data[src_idx];
            }
        }
    }

    /* Layout weights: depthwise format via weight_expand (diagonal in normal conv) */
    int8_t *wt = (int8_t *)weights;
    for (int c = 0; c < IN_C; c++) {
        for (int kh = 0; kh < KH; kh++) {
            for (int kw = 0; kw < KW; kw++) {
                int src_idx = c * KH * KW + kh * KW + kw;
                int dst_idx = dwconv2d_weight(KH, KW, IN_C, c+1, kh, kw);
                wt[dst_idx] = weight_data[src_idx];
            }
        }
    }

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

    logger_info("Submitting depthwise conv2d task to NPU...\n");
    rknpu_submit_task(&submit);

    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);
    logger_info("NPU depthwise conv2d completed.\n");

    /* Verify output */
    int mismatches = 0;
    int32_t *out = (int32_t *)output;
    for (int oh = 0; oh < OUT_H; oh++) {
        for (int ow = 0; ow < OUT_W; ow++) {
            for (int oc = 0; oc < OUT_C; oc++) {
                int out_idx = conv2d_feature_data(OUT_C, OUT_H, OUT_W, 4, oc+1, oh+1, ow+1);
                int32_t actual   = out[out_idx];
                int32_t expected = expected_output[oh * OUT_W * OUT_C + ow * OUT_C + oc];
                if (actual != expected) {
                    if (mismatches < 10)
                        logger_info("MISMATCH oh=%d ow=%d oc=%d: expected=%d actual=%d\n",
                                    oh, ow, oc, expected, actual);
                    mismatches++;
                }
            }
        }
    }

    if (mismatches == 0) {
        logger_info("DWConv2D %dx%dx%d k%dx%d → %dx%dx%d: ALL PASSED\n",
                    IN_H, IN_W, IN_C, KH, KW, OUT_H, OUT_W, OUT_C);
    } else {
        logger_error("DWConv2D: %d mismatches out of %d\n",
                     mismatches, OUT_H * OUT_W * OUT_C);
    }
}

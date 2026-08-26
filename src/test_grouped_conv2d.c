/*
 * test_grouped_conv2d.c — Grouped Conv2D NPU hardware test for RK3588
 *
 * Grouped convolution: the C_in / C_out channels are split into GROUPS equal
 * blocks and output block g convolves only with input block g. It is the
 * general form of convolution — GROUPS==1 is a regular conv, GROUPS==IN_C is
 * depthwise.
 *
 * The NPU has no runtime "grouping" input; grouping is realized purely as a
 * block-diagonal weight layout (grouped_conv2d_weight) fed to the board-verified
 * gen_conv2d_int8 conv MAC datapath. The MAC array does the compute, so this is
 * a GENUINE hardware operator, not a CPU emulation. Because the block-diagonal
 * expansion is mathematically exact, the INT32 NPU output is compared bit-exact
 * against the grouped-conv CPU golden.
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

#define IN_H   8
#define IN_W   8
#define IN_C   32
#define OUT_C  32
#define GROUPS 4                 /* 32/4 = 8 input & 8 output channels per group */
#define ICG    (IN_C / GROUPS)   /* input channels per group  */
#define OCG    (OUT_C / GROUPS)  /* output channels per group */
#define KH     3
#define KW     3
#define STRIDE_H 1
#define STRIDE_W 1
#define PAD_TOP    1
#define PAD_LEFT   1
#define PAD_BOTTOM 1
#define PAD_RIGHT  1

#define OUT_H ((IN_H + PAD_TOP + PAD_BOTTOM - KH) / STRIDE_H + 1)
#define OUT_W ((IN_W + PAD_LEFT + PAD_RIGHT - KW) / STRIDE_W + 1)

static int8_t  input_data[IN_H * IN_W * IN_C];
static int8_t  weight_data[OUT_C * ICG * KH * KW];   /* compact grouped weights [oc][icg][kh][kw] */
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

/* Grouped conv golden: output channel oc (group g = oc / OCG) sees only input
 * channels [g*ICG, (g+1)*ICG). weight_data is [oc][icg][kh][kw]. */
static void
grouped_conv2d_cpu_reference(void)
{
    for (int oh = 0; oh < OUT_H; oh++) {
        for (int ow = 0; ow < OUT_W; ow++) {
            for (int oc = 0; oc < OUT_C; oc++) {
                int g = oc / OCG;
                int64_t sum = 0;
                for (int kh = 0; kh < KH; kh++) {
                    for (int kw = 0; kw < KW; kw++) {
                        int ih = oh * STRIDE_H - PAD_TOP + kh;
                        int iw = ow * STRIDE_W - PAD_LEFT + kw;
                        if (ih < 0 || ih >= IN_H || iw < 0 || iw >= IN_W)
                            continue;
                        for (int icg = 0; icg < ICG; icg++) {
                            int ic = g * ICG + icg;
                            int8_t iv = input_data[ih * IN_W * IN_C + iw * IN_C + ic];
                            int8_t wv = weight_data[((oc * ICG + icg) * KH + kh) * KW + kw];
                            sum += (int32_t)(iv * wv);
                        }
                    }
                }
                expected_output[oh * OUT_W * OUT_C + ow * OUT_C + oc] = (int32_t)sum;
            }
        }
    }
}

void
rknpu_test_grouped_conv2d(void)
{
    logger_info("=== Grouped Conv2D NPU Test ===\n");
    logger_info("Input: %dx%dx%d, Kernel: %dx%d, Groups: %d (%d in/%d out per group)\n",
                IN_H, IN_W, IN_C, KH, KW, GROUPS, ICG, OCG);
    logger_info("Output: %dx%dx%d\n", OUT_H, OUT_W, OUT_C);

    void *regcmd  = t_mem_alloc(1024);
    npu_task_t *tasks = t_mem_alloc(1024);
    void *input   = t_mem_alloc(IN_H * IN_W * IN_C * sizeof(int8_t));
    /* Weight buffer holds the FULL block-diagonal conv weight (off-block zero). */
    void *weights = t_mem_alloc(OUT_C * KH * KW * IN_C * sizeof(int8_t));
    void *output  = t_mem_alloc(OUT_H * OUT_W * OUT_C * sizeof(int32_t));

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("Grouped Conv2D Test: Memory allocation failed\n");
        return;
    }

    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    conv2d_params_t params = {
        .in_h = IN_H, .in_w = IN_W, .in_c = IN_C,
        .out_c = OUT_C,
        .groups = GROUPS,
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

    if (gen_grouped_conv2d_int8(&params) != 0) {
        logger_error("Grouped Conv2D Test: gen_grouped_conv2d_int8 failed\n");
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
    for (int i = 0; i < OUT_C * ICG * KH * KW; i++)
        weight_data[i] = rand_int8();

    logger_info("Computing CPU reference (grouped)...\n");
    grouped_conv2d_cpu_reference();

    /* Layout input to NC1HWC2 (C2=16 for INT8) */
    int8_t *feat = (int8_t *)input;
    for (int h = 0; h < IN_H; h++)
        for (int w = 0; w < IN_W; w++)
            for (int c = 0; c < IN_C; c++)
                feat[conv2d_feature_data(IN_C, IN_H, IN_W, 16, c+1, h+1, w+1)] =
                    input_data[h * IN_W * IN_C + w * IN_C + c];

    /* Layout compact grouped weights into the block-diagonal tiled conv format.
     * ic_in_group is 1-based (1..ICG); the helper maps it to the full input
     * channel g*ICG + ic_in_group and defers to the verified conv2d_weight. */
    int8_t *wt = (int8_t *)weights;
    for (int oc = 0; oc < OUT_C; oc++)
        for (int icg = 0; icg < ICG; icg++)
            for (int kh = 0; kh < KH; kh++)
                for (int kw = 0; kw < KW; kw++) {
                    int src = ((oc * ICG + icg) * KH + kh) * KW + kw;
                    int dst = grouped_conv2d_weight(IN_C, KH, KW, OUT_C, GROUPS,
                                                    oc+1, icg+1, kh, kw, 1);
                    wt[dst] = weight_data[src];
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

    logger_info("Submitting grouped conv2d task to NPU...\n");
    rknpu_submit_task(&submit);

    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);
    logger_info("NPU grouped conv2d completed.\n");

    /* Verify output (INT32, NC1HWC2 C2=4) */
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

    if (mismatches == 0)
        logger_info("Grouped Conv2D %dx%dx%d k%dx%d g%d -> %dx%dx%d: ALL PASSED\n",
                    IN_H, IN_W, IN_C, KH, KW, GROUPS, OUT_H, OUT_W, OUT_C);
    else
        logger_error("Grouped Conv2D: %d mismatches out of %d\n",
                     mismatches, OUT_H * OUT_W * OUT_C);
}

#include "npulib/npu_pool.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"
#include "dev/t_timer.h"

#define IN_H  8
#define IN_W  8
#define IN_C  32
#define KH    2
#define KW    2
#define STRIDE_H 2
#define STRIDE_W 2
#define PAD_TOP    0
#define PAD_LEFT   0
#define PAD_BOTTOM 0
#define PAD_RIGHT  0

#define OUT_H ((IN_H + PAD_TOP + PAD_BOTTOM - KH) / STRIDE_H + 1)
#define OUT_W ((IN_W + PAD_LEFT + PAD_RIGHT - KW) / STRIDE_W + 1)
#define OUT_C IN_C

static int8_t input_data[IN_H * IN_W * IN_C];
static int8_t output_data[OUT_H * OUT_W * OUT_C];
static int8_t expected_output[OUT_H * OUT_W * OUT_C];

static int8_t
rand_int8(void)
{
    int8_t val = ((rand_tick() % 255) - 128);
    if (val > 127) val -= 128;
    if (val < -128) val += 128;
    return val;
}

static void
maxpool_cpu_reference(void)
{
    for (int oh = 0; oh < OUT_H; oh++) {
        for (int ow = 0; ow < OUT_W; ow++) {
            for (int c = 0; c < IN_C; c++) {
                int8_t max_val = -128;
                for (int kh = 0; kh < KH; kh++) {
                    for (int kw = 0; kw < KW; kw++) {
                        int ih = oh * STRIDE_H - PAD_TOP + kh;
                        int iw = ow * STRIDE_W - PAD_LEFT + kw;
                        if (ih < 0 || ih >= IN_H || iw < 0 || iw >= IN_W)
                            continue;
                        int8_t val = input_data[ih * IN_W * IN_C + iw * IN_C + c];
                        if (val > max_val)
                            max_val = val;
                    }
                }
                expected_output[oh * OUT_W * OUT_C + ow * OUT_C + c] = max_val;
            }
        }
    }
}

void
rknpu_test_maxpool(void)
{
    logger_info("=== MaxPool INT8 CPU Test ===\n");
    logger_info("Input: %dx%dx%d, Pool: %dx%d, Stride: %dx%d, Pad: %d/%d/%d/%d\n",
                IN_H, IN_W, IN_C, KH, KW, STRIDE_H, STRIDE_W,
                PAD_TOP, PAD_LEFT, PAD_BOTTOM, PAD_RIGHT);
    logger_info("Output: %dx%dx%d\n", OUT_H, OUT_W, OUT_C);

    srand_tick();
    for (int i = 0; i < IN_H * IN_W * IN_C; i++)
        input_data[i] = rand_int8();

    logger_info("Computing CPU reference (maxpool)...\n");
    maxpool_cpu_reference();

    maxpool_params_t params = {
        .in_h = IN_H, .in_w = IN_W, .in_c = IN_C,
        .kh = KH, .kw = KW,
        .stride_h = STRIDE_H, .stride_w = STRIDE_W,
        .pad_top = PAD_TOP, .pad_left = PAD_LEFT,
        .pad_bottom = PAD_BOTTOM, .pad_right = PAD_RIGHT,
        .input = input_data,
        .output = output_data,
    };

    uint32_t t0 = timer_get_system_ticks();
    maxpool_int8(&params);
    uint32_t t1 = timer_get_system_ticks();
    logger_info("MaxPool completed (%d ticks)\n", t1 - t0);

    int mismatches = 0;
    for (int i = 0; i < OUT_H * OUT_W * OUT_C; i++) {
        if (output_data[i] != expected_output[i]) {
            if (mismatches < 10)
                logger_info("MISMATCH idx=%d: expected=%d actual=%d\n",
                            i, expected_output[i], output_data[i]);
            mismatches++;
        }
    }

    if (mismatches == 0) {
        logger_info("MaxPool %dx%dx%d pool%dx%d stride%d → %dx%dx%d: ALL PASSED\n",
                    IN_H, IN_W, IN_C, KH, KW, STRIDE_H, OUT_H, OUT_W, OUT_C);
    } else {
        logger_error("MaxPool: %d mismatches out of %d\n",
                     mismatches, OUT_H * OUT_W * OUT_C);
    }
}

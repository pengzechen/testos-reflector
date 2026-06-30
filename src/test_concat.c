#include "npulib/npu_concat.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"

#define H  4
#define W  4
#define C1 16
#define C2 32
#define C3 16
#define TOTAL_C (C1 + C2 + C3)

static int8_t input1[H * W * C1];
static int8_t input2[H * W * C2];
static int8_t input3[H * W * C3];
static int8_t output_data[H * W * TOTAL_C];
static int8_t expected[H * W * TOTAL_C];

void
rknpu_test_concat(void)
{
    logger_info("=== Concat Channel INT8 Test ===\n");
    logger_info("Inputs: %dx%dx%d + %dx%dx%d + %dx%dx%d → %dx%dx%d\n",
                H, W, C1, H, W, C2, H, W, C3, H, W, TOTAL_C);

    srand_tick();
    for (int i = 0; i < H * W * C1; i++)
        input1[i] = (int8_t)((rand_tick() % 256) - 128);
    for (int i = 0; i < H * W * C2; i++)
        input2[i] = (int8_t)((rand_tick() % 256) - 128);
    for (int i = 0; i < H * W * C3; i++)
        input3[i] = (int8_t)((rand_tick() % 256) - 128);

    /* CPU reference */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int base = (y * W + x) * TOTAL_C;
            memcpy(&expected[base],           &input1[(y * W + x) * C1], C1);
            memcpy(&expected[base + C1],      &input2[(y * W + x) * C2], C2);
            memcpy(&expected[base + C1 + C2], &input3[(y * W + x) * C3], C3);
        }
    }

    concat_params_t p = {
        .h = H, .w = W,
        .num_inputs = 3,
        .inputs = {
            { .data = input1, .channels = C1 },
            { .data = input2, .channels = C2 },
            { .data = input3, .channels = C3 },
        },
        .output = output_data,
    };

    concat_channel_int8(&p);

    int mismatches = 0;
    for (int i = 0; i < H * W * TOTAL_C; i++) {
        if (output_data[i] != expected[i]) {
            if (mismatches < 10)
                logger_info("MISMATCH idx=%d: expected=%d actual=%d\n",
                            i, expected[i], output_data[i]);
            mismatches++;
        }
    }

    if (mismatches == 0)
        logger_info("Concat %dx%dx(%d+%d+%d)→%d: ALL PASSED (%d elements)\n",
                    H, W, C1, C2, C3, TOTAL_C, H * W * TOTAL_C);
    else
        logger_error("Concat: %d mismatches out of %d\n",
                     mismatches, H * W * TOTAL_C);
}

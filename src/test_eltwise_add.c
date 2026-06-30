#include "npulib/npu_eltwise.h"

#include "lib/t_logger.h"
#include "lib/rand.h"

#define H  8
#define W  8
#define C  32
#define TOTAL (H * W * C)

static int8_t input_a[TOTAL];
static int8_t input_b[TOTAL];
static int8_t output_data[TOTAL];
static int8_t expected[TOTAL];

static int8_t
saturate_add(int8_t a, int8_t b)
{
    int sum = (int)a + (int)b;
    if (sum > 127) return 127;
    if (sum < -128) return -128;
    return (int8_t)sum;
}

void
rknpu_test_eltwise_add(void)
{
    logger_info("=== Element-wise Add INT8 CPU Test ===\n");
    logger_info("Shape: %dx%dx%d (%d elements)\n", H, W, C, TOTAL);

    srand_tick();
    for (int i = 0; i < TOTAL; i++) {
        input_a[i] = (int8_t)((rand_tick() % 256) - 128);
        input_b[i] = (int8_t)((rand_tick() % 256) - 128);
    }

    for (int i = 0; i < TOTAL; i++)
        expected[i] = saturate_add(input_a[i], input_b[i]);

    eltwise_add_params_t p = {
        .h = H, .w = W, .c = C,
        .input_a = input_a,
        .input_b = input_b,
        .output = output_data,
    };

    eltwise_add_int8(&p);

    int mismatches = 0;
    for (int i = 0; i < TOTAL; i++) {
        if (output_data[i] != expected[i]) {
            if (mismatches < 10)
                logger_info("MISMATCH idx=%d: expected=%d actual=%d\n",
                            i, expected[i], output_data[i]);
            mismatches++;
        }
    }

    if (mismatches == 0)
        logger_info("Eltwise Add %dx%dx%d: ALL PASSED (%d elements)\n",
                    H, W, C, TOTAL);
    else
        logger_error("Eltwise Add: %d mismatches out of %d\n",
                     mismatches, TOTAL);
}

#include "npulib/npu_mul.h"

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

void
rknpu_test_mul(void)
{
    logger_info("=== Mul INT8 CPU Test ===\n");
    logger_info("Shape: %dx%dx%d (%d elements), scale=1 shift=7\n", H, W, C, TOTAL);

    srand_tick();
    for (int i = 0; i < TOTAL; i++) {
        input_a[i] = (int8_t)(rand_tick() & 0xFF);
        input_b[i] = (int8_t)(rand_tick() & 0xFF);
    }

    uint16_t scale = 1;
    uint8_t  shift = 7;

    for (int i = 0; i < TOTAL; i++) {
        int a = (int)(signed char)input_a[i];
        int b = (int)(signed char)input_b[i];
        int val = (a * b * (int)scale) >> shift;
        if (val > 127) val = 127;
        if (val < -128) val = -128;
        expected[i] = (int8_t)val;
    }

    mul_params_t p = {
        .h = H, .w = W, .c = C,
        .input_a = input_a,
        .input_b = input_b,
        .output  = output_data,
        .scale   = scale,
        .shift   = shift,
    };

    mul_int8(&p);

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
        logger_info("Mul %dx%dx%d: ALL PASSED (%d elements)\n", H, W, C, TOTAL);
    else
        logger_error("Mul: %d mismatches out of %d\n", mismatches, TOTAL);
}

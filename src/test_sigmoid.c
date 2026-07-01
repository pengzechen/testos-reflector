#include "npulib/npu_sigmoid.h"
#include "npulib/npu_math.h"

#include "lib/t_logger.h"
#include "lib/rand.h"

#define H  8
#define W  8
#define C  32
#define TOTAL (H * W * C)

static int8_t input_data[TOTAL];
static int8_t output_data[TOTAL];
static int8_t expected[TOTAL];

void
rknpu_test_sigmoid(void)
{
    logger_info("=== Sigmoid INT8 CPU Test ===\n");
    logger_info("Shape: %dx%dx%d (%d elements)\n", H, W, C, TOTAL);

    srand_tick();
    for (int i = 0; i < TOTAL; i++)
        input_data[i] = (int8_t)(rand_tick() & 0xFF);

    for (int i = 0; i < TOTAL; i++)
        expected[i] = sigmoid_lut[(uint8_t)input_data[i]];

    sigmoid_params_t p = {
        .h = H, .w = W, .c = C,
        .input  = input_data,
        .output = output_data,
    };

    sigmoid_int8(&p);

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
        logger_info("Sigmoid %dx%dx%d: ALL PASSED (%d elements)\n", H, W, C, TOTAL);
    else
        logger_error("Sigmoid: %d mismatches out of %d\n", mismatches, TOTAL);
}

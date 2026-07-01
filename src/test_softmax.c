#include "npulib/npu_softmax.h"
#include "npulib/npu_math.h"

#include "lib/t_logger.h"
#include "lib/rand.h"

#define OUTER    64
#define AXIS_LEN 32
#define TOTAL    (OUTER * AXIS_LEN)

static int8_t input_data[TOTAL];
static int8_t output_data[TOTAL];
static int8_t expected[TOTAL];

static void
ref_softmax(const int8_t *in, int8_t *out, int outer, int axis_len)
{
    for (int n = 0; n < outer; n++) {
        const int8_t *row_in  = in  + n * axis_len;
        int8_t       *row_out = out + n * axis_len;

        int max_val = (signed char)row_in[0];
        for (int i = 1; i < axis_len; i++) {
            int v = (signed char)row_in[i];
            if (v > max_val) max_val = v;
        }

        uint32_t sum = 0;
        for (int i = 0; i < axis_len; i++) {
            int d = max_val - (signed char)row_in[i];
            if (d > 255) d = 255;
            sum += exp_lut[d];
        }
        if (sum == 0) sum = 1;

        for (int i = 0; i < axis_len; i++) {
            int d = max_val - (signed char)row_in[i];
            if (d > 255) d = 255;
            int val = (int)((uint32_t)exp_lut[d] * 255 / sum) - 128;
            if (val > 127) val = 127;
            if (val < -128) val = -128;
            row_out[i] = (int8_t)val;
        }
    }
}

void
rknpu_test_softmax(void)
{
    logger_info("=== Softmax INT8 CPU Test ===\n");
    logger_info("Shape: outer=%d axis_len=%d (%d elements)\n",
                OUTER, AXIS_LEN, TOTAL);

    srand_tick();
    for (int i = 0; i < TOTAL; i++)
        input_data[i] = (int8_t)(rand_tick() & 0xFF);

    ref_softmax(input_data, expected, OUTER, AXIS_LEN);

    softmax_params_t p = {
        .outer    = OUTER,
        .axis_len = AXIS_LEN,
        .input    = input_data,
        .output   = output_data,
    };

    softmax_int8(&p);

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
        logger_info("Softmax %dx%d: ALL PASSED (%d elements)\n",
                    OUTER, AXIS_LEN, TOTAL);
    else
        logger_error("Softmax: %d mismatches out of %d\n", mismatches, TOTAL);
}

#include "npulib/npu_reshape.h"

#include "lib/t_logger.h"
#include "lib/rand.h"

#define TOTAL 2048

static int8_t input_data[TOTAL];
static int8_t output_data[TOTAL];

void
rknpu_test_reshape(void)
{
    logger_info("=== Reshape INT8 CPU Test ===\n");
    logger_info("Total: %d elements\n", TOTAL);

    srand_tick();
    for (int i = 0; i < TOTAL; i++)
        input_data[i] = (int8_t)(rand_tick() & 0xFF);

    reshape_params_t p = {
        .total_bytes = TOTAL,
        .input  = input_data,
        .output = output_data,
    };

    reshape_int8(&p);

    int mismatches = 0;
    for (int i = 0; i < TOTAL; i++) {
        if (output_data[i] != input_data[i]) {
            if (mismatches < 10)
                logger_info("MISMATCH idx=%d: expected=%d actual=%d\n",
                            i, input_data[i], output_data[i]);
            mismatches++;
        }
    }

    if (mismatches == 0)
        logger_info("Reshape: ALL PASSED (%d elements)\n", TOTAL);
    else
        logger_error("Reshape: %d mismatches out of %d\n", mismatches, TOTAL);
}

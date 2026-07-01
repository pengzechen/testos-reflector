#include "npu_softmax.h"
#include "npu_math.h"

void
softmax_int8(softmax_params_t *p)
{
    for (int n = 0; n < p->outer; n++) {
        const int8_t *in  = p->input  + n * p->axis_len;
        int8_t       *out = p->output + n * p->axis_len;

        int max_val = (signed char)in[0];
        for (int i = 1; i < p->axis_len; i++) {
            int v = (signed char)in[i];
            if (v > max_val) max_val = v;
        }

        uint32_t sum = 0;
        for (int i = 0; i < p->axis_len; i++) {
            int d = max_val - (signed char)in[i];
            if (d > 255) d = 255;
            sum += exp_lut[d];
        }

        if (sum == 0) sum = 1;

        for (int i = 0; i < p->axis_len; i++) {
            int d = max_val - (signed char)in[i];
            if (d > 255) d = 255;
            int val = (int)((uint32_t)exp_lut[d] * 255 / sum) - 128;
            if (val > 127) val = 127;
            if (val < -128) val = -128;
            out[i] = (int8_t)val;
        }
    }
}

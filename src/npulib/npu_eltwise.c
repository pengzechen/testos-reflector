#include "npu_eltwise.h"

void
eltwise_add_int8(eltwise_add_params_t *p)
{
    int total = p->h * p->w * p->c;
    for (int i = 0; i < total; i++) {
        int sum = (int)p->input_a[i] + (int)p->input_b[i];
        if (sum > 127) sum = 127;
        if (sum < -128) sum = -128;
        p->output[i] = (int8_t)sum;
    }
}

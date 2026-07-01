#include "npu_sigmoid.h"
#include "npu_math.h"

void
sigmoid_int8(sigmoid_params_t *p)
{
    int total = p->h * p->w * p->c;
    for (int i = 0; i < total; i++)
        p->output[i] = sigmoid_lut[(uint8_t)p->input[i]];
}

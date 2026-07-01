#include "npu_mul.h"

void
mul_int8(mul_params_t *p)
{
    int total = p->h * p->w * p->c;
    for (int i = 0; i < total; i++) {
        int a = (int)(signed char)p->input_a[i];
        int b = (int)(signed char)p->input_b[i];
        int val = (a * b * (int)p->scale) >> p->shift;
        if (val > 127) val = 127;
        if (val < -128) val = -128;
        p->output[i] = (int8_t)val;
    }
}

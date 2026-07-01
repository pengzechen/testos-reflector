#ifndef NPU_MUL_H
#define NPU_MUL_H

#include "t_types.h"

typedef struct {
    uint16_t h, w, c;
    const int8_t *input_a;
    const int8_t *input_b;
    int8_t       *output;
    uint16_t      scale;
    uint8_t       shift;
} mul_params_t;

void mul_int8(mul_params_t *p);

#endif

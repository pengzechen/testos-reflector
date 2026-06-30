#ifndef NPU_ELTWISE_H
#define NPU_ELTWISE_H

#include "t_types.h"

typedef struct {
    uint16_t h, w, c;
    const int8_t *input_a;
    const int8_t *input_b;
    int8_t       *output;
} eltwise_add_params_t;

void eltwise_add_int8(eltwise_add_params_t *p);

#endif

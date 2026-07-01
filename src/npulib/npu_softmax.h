#ifndef NPU_SOFTMAX_H
#define NPU_SOFTMAX_H

#include "t_types.h"

typedef struct {
    uint16_t outer;
    uint16_t axis_len;
    const int8_t *input;
    int8_t       *output;
} softmax_params_t;

void softmax_int8(softmax_params_t *p);

#endif

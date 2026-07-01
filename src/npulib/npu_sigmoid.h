#ifndef NPU_SIGMOID_H
#define NPU_SIGMOID_H

#include "t_types.h"

typedef struct {
    uint16_t h, w, c;
    const int8_t *input;
    int8_t       *output;
} sigmoid_params_t;

void sigmoid_int8(sigmoid_params_t *p);

#endif

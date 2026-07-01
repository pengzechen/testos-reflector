#ifndef NPU_RESHAPE_H
#define NPU_RESHAPE_H

#include "t_types.h"

typedef struct {
    uint32_t total_bytes;
    const int8_t *input;
    int8_t       *output;
} reshape_params_t;

void reshape_int8(reshape_params_t *p);

#endif

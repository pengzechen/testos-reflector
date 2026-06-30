#ifndef NPU_CONCAT_H
#define NPU_CONCAT_H

#include "t_types.h"

#define CONCAT_MAX_INPUTS 8

typedef struct {
    uint16_t h, w;
    uint8_t  num_inputs;
    struct {
        const int8_t *data;
        uint16_t      channels;
    } inputs[CONCAT_MAX_INPUTS];
    int8_t *output;
} concat_params_t;

void concat_channel_int8(concat_params_t *p);

#endif

#ifndef NPU_POOL_H
#define NPU_POOL_H

#include "t_types.h"

typedef struct {
    uint16_t in_h, in_w, in_c;
    uint8_t  kh, kw;
    uint8_t  stride_h, stride_w;
    uint8_t  pad_top, pad_left, pad_bottom, pad_right;
    uint32_t input_dma;
    uint32_t weights_dma;
    uint32_t output_dma;
    uint64_t *tasks;
} avgpool_params_t;

int  gen_avgpool_int8(avgpool_params_t *p);
void avgpool_fill_weights(int8_t *wt_buf, int channels, int kh, int kw);

typedef struct {
    uint16_t in_h, in_w, in_c;
    uint8_t  kh, kw;
    uint8_t  stride_h, stride_w;
    uint8_t  pad_top, pad_left, pad_bottom, pad_right;
    int8_t  *input;
    int8_t  *output;
} maxpool_params_t;

void maxpool_int8(maxpool_params_t *p);

#endif

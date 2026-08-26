#ifndef NPU_CONV2D_H
#define NPU_CONV2D_H

#include "t_types.h"

#define ACTIVATION_NONE  0
#define ACTIVATION_RELU  1
#define ACTIVATION_RELU6 2

typedef struct {
    uint16_t in_h, in_w, in_c;
    uint16_t out_c;
    uint16_t groups;      /* grouped conv: 0/1 = regular, in_c = depthwise. 0 treated as 1. */
    uint8_t  kh, kw;
    uint8_t  stride_h, stride_w;
    uint8_t  pad_top, pad_left, pad_bottom, pad_right;

    uint32_t input_dma;
    uint32_t weights_dma;
    uint32_t output_dma;

    uint64_t *tasks;

    uint8_t  is_int8;
    uint8_t  fp32tofp16;

    uint8_t  activation;
    uint32_t relu6_value;

    uint8_t  out_int8;
    int32_t  cvt_offset;
    uint16_t cvt_scale;
    uint8_t  cvt_shift;
} conv2d_params_t;

int gen_conv2d_int8(conv2d_params_t *params);
int gen_conv2d_fp16(conv2d_params_t *params);
int gen_dwconv2d_int8(conv2d_params_t *params);
int gen_grouped_conv2d_int8(conv2d_params_t *params);

int conv2d_feature_data(int C, int H, int W, int C2, int c, int h, int w);
int conv2d_weight(int in_c, int kh, int kw, int out_c, int oc, int ic, int krow, int kcol, int is_int8);
int dwconv2d_weight(int kh, int kw, int channels, int ch, int krow, int kcol);
int grouped_conv2d_weight(int in_c, int kh, int kw, int out_c, int groups,
                          int oc, int ic_in_group, int krow, int kcol, int is_int8);

#endif

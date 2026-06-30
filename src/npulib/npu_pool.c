#include "npu_pool.h"
#include "npu_conv2d.h"
#include "lib/t_string.h"

void
avgpool_fill_weights(int8_t *wt_buf, int channels, int kh, int kw)
{
    memset(wt_buf, 0, channels * kh * kw * channels);
    for (int c = 0; c < channels; c++)
        for (int kr = 0; kr < kh; kr++)
            for (int kc = 0; kc < kw; kc++)
                wt_buf[dwconv2d_weight(kh, kw, channels, c + 1, kr, kc)] = 1;
}

int
gen_avgpool_int8(avgpool_params_t *p)
{
    conv2d_params_t conv = {
        .in_h       = p->in_h,
        .in_w       = p->in_w,
        .in_c       = p->in_c,
        .out_c      = p->in_c,
        .kh         = p->kh,
        .kw         = p->kw,
        .stride_h   = p->stride_h,
        .stride_w   = p->stride_w,
        .pad_top    = p->pad_top,
        .pad_left   = p->pad_left,
        .pad_bottom = p->pad_bottom,
        .pad_right  = p->pad_right,
        .input_dma  = p->input_dma,
        .weights_dma = p->weights_dma,
        .output_dma = p->output_dma,
        .tasks      = p->tasks,
        .is_int8    = 1,
        .fp32tofp16 = 0,
        .activation = ACTIVATION_NONE,
    };
    return gen_dwconv2d_int8(&conv);
}

void
maxpool_int8(maxpool_params_t *p)
{
    int out_h = (p->in_h + p->pad_top + p->pad_bottom - p->kh) / p->stride_h + 1;
    int out_w = (p->in_w + p->pad_left + p->pad_right - p->kw) / p->stride_w + 1;

    for (int oh = 0; oh < out_h; oh++) {
        for (int ow = 0; ow < out_w; ow++) {
            for (int c = 0; c < p->in_c; c++) {
                int8_t max_val = -128;
                for (int kh = 0; kh < p->kh; kh++) {
                    for (int kw = 0; kw < p->kw; kw++) {
                        int ih = oh * p->stride_h - p->pad_top + kh;
                        int iw = ow * p->stride_w - p->pad_left + kw;
                        if (ih < 0 || ih >= p->in_h || iw < 0 || iw >= p->in_w)
                            continue;
                        int8_t val = p->input[ih * p->in_w * p->in_c + iw * p->in_c + c];
                        if (val > max_val)
                            max_val = val;
                    }
                }
                p->output[oh * out_w * p->in_c + ow * p->in_c + c] = max_val;
            }
        }
    }
}

/*
 * Deconvolution / ConvTranspose for the RK3588 NPU.
 *
 * See npu_deconv.h for the full reverse-engineering notes. Short version:
 *   - deconv_int8_ref()  : correct CPU scatter-add ConvTranspose (golden).
 *   - deconv_weight()    : ConvTranspose weight -> flipped+channel-swapped conv
 *                          tiled position.
 *   - gen_deconv_int8()  : lowers deconv to a stride-1 conv over the dilated
 *                          input and calls the HW-verified gen_conv2d_int8().
 *
 * Because the lowering is mathematically exact (see header), the NPU result is
 * bit-exact against deconv_int8_ref -- so the on-board test in test_deconv.c is
 * a genuine HW test, not a verify-pending scaffold (unlike npu_transpose.c).
 */

#include "npu_hw.h"
#include "npu_cna.h"
#include "npu_dpu.h"
#include "npu_conv2d.h"
#include "npu_matmul.h"
#include "npu_deconv.h"


/* ------------------------------------------------------------------------- *
 * CPU reference / golden operator.
 *
 * out[co,ho,wo] += X[ci,hi,wi] * W[ci,co,ki,kj], with
 *   ho = hi*stride_h - pad_top  + ki
 *   wo = wi*stride_w - pad_left + kj
 * over in-bounds (ho,wo). Weight layout is ConvTranspose [Cin,Cout,kh,kw].
 * ------------------------------------------------------------------------- */
void
deconv_int8_ref(const int8_t *X, const int8_t *W, int32_t *Y,
                int Cin, int Hin, int Win, int Cout,
                int kh, int kw, int sh, int sw,
                int pad_t, int pad_l, int pad_b, int pad_r,
                int oph, int opw)
{
    int Hout = (Hin - 1) * sh - (pad_t + pad_b) + kh + oph;
    int Wout = (Win - 1) * sw - (pad_l + pad_r) + kw + opw;

    for (int i = 0; i < Cout * Hout * Wout; i++)
        Y[i] = 0;

    for (int ci = 0; ci < Cin; ci++) {
        for (int hi = 0; hi < Hin; hi++) {
            for (int wi = 0; wi < Win; wi++) {
                int8_t xval = X[(ci * Hin + hi) * Win + wi];
                if (xval == 0)
                    continue;
                for (int co = 0; co < Cout; co++) {
                    for (int ki = 0; ki < kh; ki++) {
                        int ho = hi * sh - pad_t + ki;
                        if (ho < 0 || ho >= Hout)
                            continue;
                        for (int kj = 0; kj < kw; kj++) {
                            int wo = wi * sw - pad_l + kj;
                            if (wo < 0 || wo >= Wout)
                                continue;
                            int8_t wval = W[((ci * Cout + co) * kh + ki) * kw + kj];
                            Y[(co * Hout + ho) * Wout + wo] += (int32_t)(xval * wval);
                        }
                    }
                }
            }
        }
    }
}


/* ------------------------------------------------------------------------- *
 * Weight remap: ConvTranspose W[ci][co][ki][kj]  ->  conv W'[co][ci][i][j]
 * with W'[co][ci][i][j] = W[ci][co][kh-1-i][kw-1-j] (spatial 180 flip + swap
 * of the in/out channel roles). We place the element at the conv position
 * (oc=co, ic=ci, krow=kh-1-ki, kcol=kw-1-kj).
 * ------------------------------------------------------------------------- */
int
deconv_weight(int in_c, int kh, int kw, int out_c, int co, int ci, int ki, int kj, int is_int8)
{
    return conv2d_weight(in_c, kh, kw, out_c, co, ci, kh - 1 - ki, kw - 1 - kj, is_int8);
}


/* ------------------------------------------------------------------------- *
 * HW path: lower deconv to a stride-1 conv on the dilated input.
 * ------------------------------------------------------------------------- */
int
gen_deconv_int8(deconv_params_t *p)
{
    /* Effective conv padding on the dilated input. Must be non-negative:
     * the direct conv engine cannot express negative padding (that would be a
     * crop, which ConvTranspose with pad > kernel-1 would require). */
    int conv_pad_top    = deconv_conv_pad_top(p);
    int conv_pad_bottom = deconv_conv_pad_bottom(p);
    int conv_pad_left   = deconv_conv_pad_left(p);
    int conv_pad_right  = deconv_conv_pad_right(p);

    if (conv_pad_top < 0 || conv_pad_bottom < 0 || conv_pad_left < 0 || conv_pad_right < 0)
        return -3;

    conv2d_params_t c = {
        /* Align-4-padded dilated dims: the surf_stride register uses integer
         * /4 and is only valid for feature heights that are multiples of 4
         * (see npu_deconv.h). Trailing zero rows/cols only feed discarded
         * output positions, so the valid region stays bit-exact. */
        .in_h        = (uint16_t)deconv_conv_in_h(p),
        .in_w        = (uint16_t)deconv_conv_in_w(p),
        .in_c        = p->in_c,
        .out_c       = p->out_c,
        .kh          = p->kh,
        .kw          = p->kw,
        .stride_h    = 1,
        .stride_w    = 1,
        .pad_top     = (uint8_t)conv_pad_top,
        .pad_left    = (uint8_t)conv_pad_left,
        .pad_bottom  = (uint8_t)conv_pad_bottom,
        .pad_right   = (uint8_t)conv_pad_right,
        .input_dma   = p->input_dma,
        .weights_dma = p->weights_dma,
        .output_dma  = p->output_dma,
        .tasks       = p->tasks,
        .is_int8     = 1,
        .fp32tofp16  = 0,
        .activation  = ACTIVATION_NONE,
        .out_int8    = p->out_int8,
        .cvt_offset  = p->cvt_offset,
        .cvt_scale   = p->cvt_scale,
        .cvt_shift   = p->cvt_shift,
    };

    return gen_conv2d_int8(&c);
}

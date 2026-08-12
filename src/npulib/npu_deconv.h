#ifndef NPU_DECONV_H
#define NPU_DECONV_H

#include "t_types.h"

/*
 * ConvTranspose / Deconvolution for the RK3588 NPU.
 *
 * Reverse-engineering notes (librknnrt.so):
 *   RKNPUEmitter_emit_deconv @0x5f51c0 shares conv's geometry/tiling engine:
 *   every one of its config-error strings prints the SAME layer parameters as
 *   conv (ori_Ih/Iw/Ic, ori_Kh/Kw, ori_Ksx/Ksy, ori_Oh/Ow/Oc, pad_*,
 *   dilation_*) PLUS an is_deconv flag -- i.e. deconv is conv with a mode bit,
 *   riding the same ABC GEMM datapath (matches [[reference_operator_capability_map]]).
 *
 * Implementation strategy (this file):
 *   ConvTranspose is mathematically EXACT as a stride-1 direct convolution over
 *   a dilated + padded input using a 180-degree-flipped, in/out-channel-swapped
 *   kernel. We therefore lower deconv onto the already-HW-verified conv2d path
 *   (gen_conv2d_int8) -- exactly the precedent set by gen_dwconv2d_int8, which
 *   reuses conv via weight-expand. This makes the NPU output bit-exact against
 *   the CPU scatter-add golden (deconv_int8_ref), so the on-board test is a real
 *   HW test, not a verify-pending scaffold.
 *
 * Equivalence (per spatial axis, stride s, kernel k, pad p, output_padding op):
 *   dilated input size  Hd = (Hin-1)*s + 1        (insert s-1 zeros between rows)
 *   conv pad (top/left) = k-1-p
 *   conv pad (bot/right)= k-1-p + op
 *   conv (stride 1) out = Hd + pad_lo + pad_hi - k + 1
 *                       = (Hin-1)*s - 2p + k + op   == deconv output size.
 *   flipped weight  W'[co][ci][i][j] = W[ci][co][k-1-i][k-1-j]
 *   ConvTranspose weight layout is [in_c, out_c, kh, kw].
 *
 * The caller lays out (a) the DILATED input in NC1HWC2 with dims
 * [in_c, deconv_dilated_h(), deconv_dilated_w()] and (b) the flipped weights via
 * deconv_weight(); gen_deconv_int8() fills the equivalent conv descriptor and
 * calls gen_conv2d_int8().
 */

typedef struct {
    uint16_t in_h, in_w;   /* ORIGINAL (undilated) input spatial size */
    uint16_t in_c, out_c;
    uint8_t  kh, kw;
    uint8_t  stride_h, stride_w;
    uint8_t  pad_top, pad_left, pad_bottom, pad_right; /* deconv pad crops output */
    uint8_t  output_padding_h, output_padding_w;

    uint32_t input_dma;    /* DILATED input, NC1HWC2, [in_c, dil_h, dil_w] */
    uint32_t weights_dma;  /* flipped+channel-swapped conv weights (deconv_weight) */
    uint32_t output_dma;

    uint64_t *tasks;

    uint8_t  is_int8;
    uint8_t  out_int8;     /* 0 => INT32 output (default) */
    int32_t  cvt_offset;
    uint16_t cvt_scale;
    uint8_t  cvt_shift;
} deconv_params_t;

/* Output spatial size of the deconvolution. */
static inline int
deconv_out_h(const deconv_params_t *p)
{
    return (int)(p->in_h - 1) * p->stride_h - (p->pad_top + p->pad_bottom) + p->kh +
           p->output_padding_h;
}
static inline int
deconv_out_w(const deconv_params_t *p)
{
    return (int)(p->in_w - 1) * p->stride_w - (p->pad_left + p->pad_right) + p->kw +
           p->output_padding_w;
}

/* Dilated input size fed to the equivalent stride-1 convolution. */
static inline int
deconv_dilated_h(const deconv_params_t *p)
{
    return (int)(p->in_h - 1) * p->stride_h + 1;
}
static inline int
deconv_dilated_w(const deconv_params_t *p)
{
    return (int)(p->in_w - 1) * p->stride_w + 1;
}

/*
 * ALIGNMENT REQUIREMENT (root cause of an earlier HW failure).
 *
 * The RK3588 conv/matmul feature-cube surf_stride register is filled as
 *   line_stride  = datain_width  * 4
 *   surf_stride  = line_stride * (datain_height / 4 - 1)      // INTEGER /4
 * (see gen_conv2d_int8 / gen_matmul_int8 in npu_conv2d.c / npu_matmul.c).
 *
 * That floor-division is only self-consistent when datain_height is a multiple
 * of 4: the HW addresses ceil(H/4) row-surfaces, but the formula strides by
 * floor(H/4)-1. For H a multiple of 4 the two agree. For H=7 the HW reads 2
 * surfaces yet surf_stride collapses to line_stride*(1-1)=0, so the second
 * surface (feature rows 4..6, i.e. input channels' upper half of the cube) is
 * fetched from the wrong DRAM offset -> every output is a partially-correct sum
 * ("close but wrong"). H=4 is fine (single surface, like matmul's M=1 case).
 *
 * A plain deconv dilates the input to Hd=(Hin-1)*s+1, which is frequently NOT a
 * multiple of 4 (e.g. 4x4 s2 -> 7x7). We therefore feed the conv engine an
 * align-4-padded dilated cube; the extra trailing zero rows/cols only feed
 * output positions we discard, so the valid [0,out_h)x[0,out_w) region is
 * bit-exact. Every HW-verified conv/pool test uses H=W=8, matching this.
 */
static inline int
deconv_align4(int x)
{
    return (x + 3) & ~3;
}

/* Effective (non-negative) conv padding applied to the dilated input. */
static inline int
deconv_conv_pad_top(const deconv_params_t *p)
{
    return (int)p->kh - 1 - p->pad_top;
}
static inline int
deconv_conv_pad_bottom(const deconv_params_t *p)
{
    return (int)p->kh - 1 - p->pad_bottom + p->output_padding_h;
}
static inline int
deconv_conv_pad_left(const deconv_params_t *p)
{
    return (int)p->kw - 1 - p->pad_left;
}
static inline int
deconv_conv_pad_right(const deconv_params_t *p)
{
    return (int)p->kw - 1 - p->pad_right + p->output_padding_w;
}

/*
 * Physical feature-cube dims fed to the conv engine: the dilated input padded
 * up to a multiple of 4 (see the alignment note above). The caller MUST lay out
 * the NC1HWC2 input cube and size the output buffer using these padded dims.
 */
static inline int
deconv_conv_in_h(const deconv_params_t *p)
{
    return deconv_align4(deconv_dilated_h(p));
}
static inline int
deconv_conv_in_w(const deconv_params_t *p)
{
    return deconv_align4(deconv_dilated_w(p));
}

/*
 * Conv output-cube dims (stride-1 conv over the padded dilated input). Used for
 * output-buffer sizing and NC1HWC2 readback stride. The valid deconv result
 * occupies the top-left [0,deconv_out_h)x[0,deconv_out_w) subregion; the rest is
 * garbage produced by the trailing zero padding and must be ignored.
 */
static inline int
deconv_conv_out_h(const deconv_params_t *p)
{
    return deconv_conv_in_h(p) + deconv_conv_pad_top(p) + deconv_conv_pad_bottom(p) - (int)p->kh + 1;
}
static inline int
deconv_conv_out_w(const deconv_params_t *p)
{
    return deconv_conv_in_w(p) + deconv_conv_pad_left(p) + deconv_conv_pad_right(p) - (int)p->kw + 1;
}

/*
 * Generate the NPU register block for a deconvolution by lowering it to a
 * stride-1 conv on the dilated input. Returns 0 on success, or the negative
 * gen_conv2d_int8 error code (-1 feature banks overflow, -2 weight too large),
 * or -3 if the effective conv padding would be negative (kernel too small for
 * the requested deconv pad -- unsupported by the direct conv engine).
 */
int gen_deconv_int8(deconv_params_t *p);

/*
 * Map a ConvTranspose weight element W[ci][co][ki][kj] to the tiled conv
 * position for the equivalent flipped+channel-swapped kernel. co/ci are
 * 1-based (like conv2d_weight's oc/ic); ki/kj are 0-based kernel coords.
 */
int deconv_weight(int in_c, int kh, int kw, int out_c, int co, int ci, int ki, int kj, int is_int8);

/*
 * CPU golden reference: textbook ConvTranspose scatter-add on plain row-major
 * buffers (independent of the NPU tiled layout). X is [Cin][Hin][Win], W is
 * [Cin][Cout][kh][kw], Y is [Cout][Hout][Wout] and is zeroed by this call.
 */
void deconv_int8_ref(const int8_t *X, const int8_t *W, int32_t *Y,
                     int Cin, int Hin, int Win, int Cout,
                     int kh, int kw, int sh, int sw,
                     int pad_t, int pad_l, int pad_b, int pad_r,
                     int oph, int opw);

#endif

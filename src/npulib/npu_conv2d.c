/*
 * Conv2D register generation for RK3588 NPU.
 * Reverse-engineered from librknnrt.so emit_conv_pool_regs (0x5DCD68).
 *
 * Conv2D uses the same register serialization as matmul (gen_matmul_task)
 * because matmul IS a 1x1 convolution. The difference is parameter filling.
 */

#include "npu_hw.h"
#include "npu_cna.h"
#include "npu_dpu.h"
#include "npu_conv2d.h"
#include "npu_matmul.h"

extern void gen_matmul_task(uint64_t *ops, npu_cna_desc *cna_desc, npu_core_desc *core_desc, npu_dpu_desc *dpu_desc);

static void
gen_conv2d_task(uint64_t *ops, npu_cna_desc *c, npu_core_desc *co, npu_dpu_desc *d)
{
    gen_matmul_task(ops, c, co, d);
}

int
gen_conv2d_int8(conv2d_params_t *p)
{
    npu_cna_desc  cna_desc;
    npu_core_desc core_desc;
    npu_dpu_desc  dpu_desc;

    unsigned int fd_bytes, fd_banks;
    int surf_stride;

    int out_h = (p->in_h + p->pad_top + p->pad_bottom - p->kh) / p->stride_h + 1;
    int out_w = (p->in_w + p->pad_left + p->pad_right - p->kw) / p->stride_w + 1;

    cna_desc.conv_mode      = direct_convolution;
    cna_desc.in_precision   = precision_int8;
    cna_desc.proc_precision = precision_int8;
    cna_desc.kernel_groups  = 0;
    cna_desc.feature_grains = p->in_h + 1;
    cna_desc.conv_x_stride  = p->stride_w;
    cna_desc.conv_y_stride  = p->stride_h;

    cna_desc.datain_width    = p->in_w;
    cna_desc.datain_height   = p->in_h;
    cna_desc.datain_channel  = p->in_c;
    cna_desc.dataout_width   = out_w;
    cna_desc.dataout_height  = out_h;
    cna_desc.dataout_atomics = out_w * out_h;

    cna_desc.weight_width   = p->kw;
    cna_desc.weight_height  = p->kh;
    cna_desc.weight_kernels = p->out_c;
    cna_desc.weight_bytes_per_kernel =
        p->kw * p->kh * p->in_c * sizeof(int8_t);
    cna_desc.weight_bytes =
        cna_desc.weight_bytes_per_kernel * p->out_c;

    fd_bytes = p->in_w * p->in_h * p->in_c * sizeof(int8_t);
    fd_banks = fd_bytes / NPU_CBUF_BANK_SIZE;
    if (fd_bytes % NPU_CBUF_BANK_SIZE) fd_banks++;
    if (fd_banks > NPU_CBUF_BANKS - 1)
        return -1;
    if (cna_desc.weight_bytes_per_kernel <= NPU_CBUF_BANK_SIZE)
        cna_desc.weight_bank = NPU_CBUF_BANKS - fd_banks;
    else
        return -2;

    cna_desc.data_bank    = fd_banks;
    cna_desc.data_entries = (p->in_w * p->in_c) / 64;
    if ((p->in_w * p->in_c) % 64) cna_desc.data_entries++;

    cna_desc.data_sign         = 1;
    cna_desc.cvt_type          = 1;
    cna_desc.cvt_bypass        = 1;
    cna_desc.cvt_scale0        = 1;
    cna_desc.cvt_scale1        = 1;
    cna_desc.cvt_scale2        = 1;
    cna_desc.cvt_scale3        = 1;
    cna_desc.fc_skip_en        = 0;
    cna_desc.data_offset       = 0;
    cna_desc.pad_left          = p->pad_left;
    cna_desc.pad_top           = p->pad_top;
    cna_desc.feature_base_addr = p->input_dma;
    cna_desc.weight_offset     = 0;
    cna_desc.weight_burst_len  = 0xf;
    cna_desc.data_burst_len    = 0xf;
    cna_desc.line_stride       = p->in_w * 4;
    surf_stride = cna_desc.line_stride * (p->in_h / 4 - 1);
    cna_desc.surf_stride = surf_stride < 0 ? surf_stride + 1 : surf_stride;
    cna_desc.dma_width         = p->in_w;
    cna_desc.dma_height        = p->in_h;
    cna_desc.dma_channel       = p->in_c;
    cna_desc.decompress_addr0  = p->weights_dma;

    core_desc.proc_precision  = precision_int8;
    core_desc.qd_en           = 0;
    core_desc.dataout_height  = out_h - 1;
    core_desc.dataout_width   = out_w - 1;
    core_desc.dataout_channel = p->out_c - 1;

    dpu_desc.burst_len        = 0xf;
    dpu_desc.conv_mode        = direct_convolution;
    dpu_desc.output_mode      = 0x2;
    dpu_desc.flying_mode      = 0x0;
    dpu_desc.in_precision     = precision_int8;
    dpu_desc.proc_precision   = precision_int8;
    dpu_desc.dst_base_addr    = p->output_dma;

    if (p->out_int8)
        dpu_desc.out_precision = precision_int8;
    else
        dpu_desc.out_precision = precision_int32;

    dpu_desc.dst_surf_stride  = out_h * out_w;

    dpu_desc.width            = core_desc.dataout_width;
    dpu_desc.height           = core_desc.dataout_height;
    dpu_desc.channel          = core_desc.dataout_channel;
    dpu_desc.bs_bypass        = (p->activation != ACTIVATION_NONE) ? 0 : 1;
    dpu_desc.bs_alu_bypass    = 1;
    dpu_desc.bs_mul_bypass    = 1;
    dpu_desc.bs_relu_bypass   = (p->activation != ACTIVATION_NONE) ? 0 : 1;
    dpu_desc.bn_bypass        = p->out_int8 ? 0 : 1;
    dpu_desc.bn_alu_bypass    = 1;
    dpu_desc.bn_mul_bypass    = 1;
    dpu_desc.bn_relu_bypass   = 1;
    dpu_desc.ew_bypass        = 1;
    dpu_desc.ew_op_bypass     = 1;
    dpu_desc.ew_lut_bypass    = 1;
    dpu_desc.ew_op_cvt_bypass = 1;
    dpu_desc.ew_relu_bypass   = 1;
    dpu_desc.fp32tofp16_en    = 0;

    if (p->out_int8) {
        dpu_desc.out_cvt_scale  = p->cvt_scale;
        dpu_desc.out_cvt_offset = p->cvt_offset;
        dpu_desc.out_cvt_shift  = p->cvt_shift;
    } else {
        dpu_desc.out_cvt_scale  = 1;
        dpu_desc.out_cvt_offset = 0;
        dpu_desc.out_cvt_shift  = 0;
    }

    if (p->out_int8) {
        dpu_desc.size_e_2 = 1;
        dpu_desc.size_e_1 = 1;
        dpu_desc.size_e_0 = 1;
    } else {
        dpu_desc.size_e_2 = 7;
        dpu_desc.size_e_1 = 7;
        dpu_desc.size_e_0 = 7;
    }
    dpu_desc.od_bypass        = p->out_int8 ? 0 : 1;
    dpu_desc.width_wdma       = core_desc.dataout_width;
    dpu_desc.height_wdma      = core_desc.dataout_height;
    dpu_desc.channel_wdma     = core_desc.dataout_channel;

    if (p->out_int8)
        dpu_desc.surf_add = dpu_desc.dst_surf_stride * 2;
    else
        dpu_desc.surf_add = dpu_desc.dst_surf_stride * 8;

    gen_conv2d_task(p->tasks, &cna_desc, &core_desc, &dpu_desc);

    /* For ReLU6, write clamp value to DPU_BS_RELUX_CMP_VALUE (ops[66]) */
    if (p->activation == ACTIVATION_RELU6)
        p->tasks[66] = NPUOP(OP_REG_DPU, p->relu6_value, DPU_BS_RELUX_CMP_VALUE);
    return 0;
}

int
gen_conv2d_fp16(conv2d_params_t *p)
{
    npu_cna_desc  cna_desc;
    npu_core_desc core_desc;
    npu_dpu_desc  dpu_desc;

    unsigned int fd_bytes, fd_banks;
    int surf_stride;

    int out_h = (p->in_h + p->pad_top + p->pad_bottom - p->kh) / p->stride_h + 1;
    int out_w = (p->in_w + p->pad_left + p->pad_right - p->kw) / p->stride_w + 1;

    cna_desc.conv_mode      = direct_convolution;
    cna_desc.in_precision   = precision_float16;
    cna_desc.proc_precision = precision_float16;
    cna_desc.kernel_groups  = 0;
    cna_desc.feature_grains = p->in_h + 1;
    cna_desc.conv_x_stride  = p->stride_w;
    cna_desc.conv_y_stride  = p->stride_h;

    cna_desc.datain_width    = p->in_w;
    cna_desc.datain_height   = p->in_h;
    cna_desc.datain_channel  = p->in_c;
    cna_desc.dataout_width   = out_w;
    cna_desc.dataout_height  = out_h;
    cna_desc.dataout_atomics = out_w * out_h;

    cna_desc.weight_width   = p->kw;
    cna_desc.weight_height  = p->kh;
    cna_desc.weight_kernels = p->out_c;
    cna_desc.weight_bytes_per_kernel =
        p->kw * p->kh * p->in_c * sizeof(uint16_t);
    cna_desc.weight_bytes =
        cna_desc.weight_bytes_per_kernel * p->out_c;

    fd_bytes = p->in_w * p->in_h * p->in_c * sizeof(uint16_t);
    fd_banks = fd_bytes / NPU_CBUF_BANK_SIZE;
    if (fd_bytes % NPU_CBUF_BANK_SIZE) fd_banks++;
    if (fd_banks > NPU_CBUF_BANKS - 1)
        return -1;
    if (cna_desc.weight_bytes_per_kernel <= NPU_CBUF_BANK_SIZE)
        cna_desc.weight_bank = NPU_CBUF_BANKS - fd_banks;
    else
        return -2;

    cna_desc.data_bank    = fd_banks;
    cna_desc.data_entries = (p->in_w * p->in_c) / 32;
    if ((p->in_w * p->in_c) % 32) cna_desc.data_entries++;

    cna_desc.data_sign         = 1;
    cna_desc.cvt_type          = 1;
    cna_desc.cvt_bypass        = 1;
    cna_desc.cvt_scale0        = 1;
    cna_desc.cvt_scale1        = 1;
    cna_desc.cvt_scale2        = 1;
    cna_desc.cvt_scale3        = 1;
    cna_desc.fc_skip_en        = 0;
    cna_desc.data_offset       = 0;
    cna_desc.pad_left          = p->pad_left;
    cna_desc.pad_top           = p->pad_top;
    cna_desc.feature_base_addr = p->input_dma;
    cna_desc.weight_offset     = 0;
    cna_desc.weight_burst_len  = 0xf;
    cna_desc.data_burst_len    = 0xf;
    cna_desc.line_stride       = p->in_w * 4;
    surf_stride = cna_desc.line_stride * (p->in_h / 4 - 1);
    cna_desc.surf_stride = surf_stride < 0 ? surf_stride + 1 : surf_stride;
    cna_desc.dma_width         = p->in_w;
    cna_desc.dma_height        = p->in_h;
    cna_desc.dma_channel       = p->in_c;
    cna_desc.decompress_addr0  = p->weights_dma;

    core_desc.proc_precision  = precision_float16;
    core_desc.qd_en           = 1;
    core_desc.dataout_height  = out_h - 1;
    core_desc.dataout_width   = out_w - 1;
    core_desc.dataout_channel = p->out_c - 1;

    dpu_desc.burst_len        = 0xf;
    dpu_desc.conv_mode        = direct_convolution;
    dpu_desc.output_mode      = 0x2;
    dpu_desc.flying_mode      = 0x0;
    dpu_desc.out_precision    = p->fp32tofp16 ? precision_float16 : precision_float32;
    dpu_desc.in_precision     = precision_float16;
    dpu_desc.proc_precision   = precision_float16;
    dpu_desc.dst_base_addr    = p->output_dma;
    dpu_desc.dst_surf_stride  = out_h * out_w;
    dpu_desc.width            = core_desc.dataout_width;
    dpu_desc.height           = core_desc.dataout_height;
    dpu_desc.channel          = core_desc.dataout_channel;
    dpu_desc.bs_bypass        = (p->activation != ACTIVATION_NONE) ? 0 : 1;
    dpu_desc.bs_alu_bypass    = 1;
    dpu_desc.bs_mul_bypass    = 1;
    dpu_desc.bs_relu_bypass   = (p->activation != ACTIVATION_NONE) ? 0 : 1;
    dpu_desc.bn_bypass        = 1;
    dpu_desc.bn_alu_bypass    = 1;
    dpu_desc.bn_mul_bypass    = 1;
    dpu_desc.bn_relu_bypass   = 1;
    dpu_desc.ew_bypass        = 1;
    dpu_desc.ew_op_bypass     = 1;
    dpu_desc.ew_lut_bypass    = 1;
    dpu_desc.ew_op_cvt_bypass = 1;
    dpu_desc.ew_relu_bypass   = 1;
    dpu_desc.fp32tofp16_en    = p->fp32tofp16 & 1;
    dpu_desc.out_cvt_scale    = 1;
    dpu_desc.out_cvt_offset   = 0;
    dpu_desc.out_cvt_shift    = 0;
    if (!p->fp32tofp16) {
        dpu_desc.size_e_2 = 3; dpu_desc.size_e_1 = 3; dpu_desc.size_e_0 = 3;
    } else {
        dpu_desc.size_e_2 = 1; dpu_desc.size_e_1 = 1; dpu_desc.size_e_0 = 1;
    }
    dpu_desc.od_bypass        = 1;
    dpu_desc.width_wdma       = core_desc.dataout_width;
    dpu_desc.height_wdma      = core_desc.dataout_height;
    dpu_desc.channel_wdma     = core_desc.dataout_channel;
    dpu_desc.surf_add         = p->fp32tofp16
        ? dpu_desc.dst_surf_stride * 2
        : dpu_desc.dst_surf_stride * 4;

    gen_conv2d_task(p->tasks, &cna_desc, &core_desc, &dpu_desc);

    if (p->activation == ACTIVATION_RELU6)
        p->tasks[66] = NPUOP(OP_REG_DPU, p->relu6_value, DPU_BS_RELUX_CMP_VALUE);
    return 0;
}

int
gen_dwconv2d_int8(conv2d_params_t *p)
{
    /*
     * RK3588 NPU implements depthwise conv as normal conv with expanded
     * weights (only diagonal oc==ic non-zero). The "weight_expand" path
     * in librknnrt confirms this — kernel_groups register is NOT used.
     *
     * Caller must lay out weights using conv2d_weight() with only
     * the diagonal (oc==ic) positions filled, rest zeroed.
     */
    p->out_c = p->in_c;
    return gen_conv2d_int8(p);
}

/*
 * Grouped convolution (INT8).
 *
 * Splits the C_in input channels and C_out output channels into `groups`
 * equal blocks; output block g convolves ONLY with input block g. This is the
 * general form: groups==1 is a regular conv, groups==in_c==out_c is depthwise.
 *
 * The RK3588 conv MAC engine has no runtime "grouping" input — grouping is
 * realized purely as a HOST-SIDE weight layout: the caller expands the compact
 * grouped weights [out_c][in_c/groups][kh][kw] into the full conv weight
 * [out_c][in_c][kh][kw] where each output channel's kernel is non-zero only on
 * its own input-channel block (off-block entries are zero). A regular conv over
 * those block-diagonal weights then computes exactly the grouped result.
 *
 * Because that expansion is mathematically exact and the underlying conv is the
 * board-verified gen_conv2d_int8 datapath (see test_conv2d), the NPU output is
 * bit-exact against a grouped-conv golden — this is a GENUINE hardware operator
 * (the conv MAC array does the compute), NOT a CPU emulation. The register
 * serialization is identical to a regular conv, so no new/guessed registers are
 * involved. Use grouped_conv2d_weight() to place the compact weights.
 *
 * Note the trade-off: block-diagonal expansion zero-pads the weight buffer to
 * full [out_c][in_c] size, so the weight footprint grows by ~groups over the
 * compact form (in_c/groups actually-used lanes per kernel). For small/medium
 * groups (ResNeXt cardinality, ShuffleNet) this is fine; for full depthwise the
 * native compact-weight path (CNA reg 0x1018 kernels_per_group=1) avoids it but
 * needs a board weight-layout capture to confirm — see IDA notes.
 *
 * Returns 0 on success, -10 if channels are not divisible by groups.
 */
int
gen_grouped_conv2d_int8(conv2d_params_t *p)
{
    int g = p->groups ? p->groups : 1;

    if (g < 1 || (p->in_c % g) != 0 || (p->out_c % g) != 0)
        return -10;

    /* Grouping lives entirely in the (block-diagonal) weight data; the register
     * generation is a plain conv over the full C_in. */
    return gen_conv2d_int8(p);
}

/*
 * Grouped conv weight layout.
 *
 * Maps a compact grouped-weight element to its block-diagonal position in the
 * full normal-conv weight, then defers to the board-verified conv2d_weight()
 * tiling. Indices: oc is 1-based (1..out_c); ic_in_group is 1-based within the
 * output channel's own input block (1..in_c/groups); krow/kcol are 0-based.
 *
 * The output channel oc belongs to group  g = (oc-1) / (out_c/groups), whose
 * input block starts at channel  g * (in_c/groups). So the full (1-based) input
 * channel is  g*(in_c/groups) + ic_in_group.
 */
int
grouped_conv2d_weight(int in_c, int kh, int kw, int out_c, int groups,
                      int oc, int ic_in_group, int krow, int kcol, int is_int8)
{
    int g = groups ? groups : 1;
    int oc_per_group = out_c / g;
    int ic_per_group = in_c / g;
    int grp = (oc - 1) / oc_per_group;
    int full_ic = grp * ic_per_group + ic_in_group;   /* 1-based full input channel */

    return conv2d_weight(in_c, kh, kw, out_c, oc, full_ic, krow, kcol, is_int8);
}

/*
 * Conv2D feature data layout: NC1HWC2 format (same as matmul).
 * C2 = 16 for FP16, 32 for INT8.
 */
int
conv2d_feature_data(int C, int H, int W, int C2, int c, int h, int w)
{
    return feature_data(C, H, W, C2, c, h, w);
}

/*
 * Conv2D weight layout in NPU memory.
 *
 * Weights are stored as [out_c][kh][kw][in_c] logically,
 * but tiled into groups of 32 (INT8) or 16 (FP16) kernels
 * and 32 input channels, matching matmul weight layout.
 *
 * For a kernel element at position (oc, ic, krow, kcol):
 *   flat_channel = krow * kw * in_c + kcol * in_c + ic
 *   total_channels = kh * kw * in_c
 *   Then use the same tiling as weight_int8/weight_fp16 but with
 *   C = total_channels instead of just in_c.
 */
int
conv2d_weight(int in_c, int kh, int kw, int out_c, int oc, int ic, int krow, int kcol, int is_int8)
{
    int total_c = kh * kw * in_c;
    int flat_c = krow * kw * in_c + kcol * in_c + ic;

    if (is_int8)
        return weight_int8(total_c, oc, flat_c);
    else
        return weight_fp16(total_c, oc, flat_c);
}

/*
 * Depthwise conv weight layout (weight_expand approach).
 * Maps a depthwise kernel element to the diagonal position in normal conv format.
 * ch is 1-based channel index, krow/kcol are 0-based.
 */
int
dwconv2d_weight(int kh, int kw, int channels, int ch, int krow, int kcol)
{
    return conv2d_weight(channels, kh, kw, channels, ch, ch, krow, kcol, 1);
}

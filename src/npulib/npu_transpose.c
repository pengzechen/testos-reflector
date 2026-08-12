/*
 * Transpose primitive for the RK3588 NPU.
 *
 * See npu_transpose.h for the full reverse-engineering notes. Short version:
 *   - transpose_int8()       : correct general 4D CPU transpose. Ships now.
 *   - transpose2d_int8_ref() : CPU golden for the 2D transpose Y = X^T.
 *   - gen_transpose2d_int8() : REAL on-NPU 2D transpose, lowered to an
 *                              identity-activation matmul (rides the verified
 *                              gen_matmul_int8 path -> bit-exact, genuinely on
 *                              hardware). This is what the on-board test runs.
 *   - gen_transpose_int8()   : HW scaffold for the NATIVE ABC datamove/copy of
 *                              perm [0,2,1,3]. HW-VERIFY-PENDING (copy-mode
 *                              config bits live behind chip-specific vtable
 *                              slots). Kept for the RE record only.
 */

#include "npu_hw.h"
#include "npu_cna.h"
#include "npu_dpu.h"
#include "npu_matmul.h"
#include "npu_transpose.h"

/* Defined in npu_matmul.c -- the shared ABC register serializer (108 ops).
 * Transpose rides the same block as matmul/conv (matches npu_conv2d.c). */
extern void gen_matmul_task(uint64_t *ops, npu_cna_desc *cna_desc,
                            npu_core_desc *core_desc, npu_dpu_desc *dpu_desc);


/* ------------------------------------------------------------------------- *
 * CPU reference / working operator.
 *
 * General 4D transpose for arbitrary perm, row-major layout, elem_size bytes
 * per element. This is the golden reference the HW path is checked against,
 * and is itself a legitimate operator in the same vein as reshape_int8 /
 * softmax / sigmoid (ops the NPU does NOT accelerate go here on the CPU).
 * ------------------------------------------------------------------------- */
void
transpose_int8(transpose_params_t *p)
{
    const uint8_t *in  = (const uint8_t *)p->input;
    uint8_t       *out = (uint8_t *)p->output;
    unsigned       es  = p->elem_size ? p->elem_size : 1;

    unsigned id[4] = { p->dims[0], p->dims[1], p->dims[2], p->dims[3] };

    /* Output dims: od[k] = id[perm[k]]. */
    unsigned od[4];
    for (int k = 0; k < 4; k++)
        od[k] = id[p->perm[k]];

    /* Row-major strides for input and output cubes (in elements). */
    unsigned is[4], os[4];
    is[3] = 1;         is[2] = id[3];         is[1] = is[2] * id[2]; is[0] = is[1] * id[1];
    os[3] = 1;         os[2] = od[3];         os[1] = os[2] * od[2]; os[0] = os[1] * od[1];

    unsigned c[4];
    for (c[0] = 0; c[0] < id[0]; c[0]++)
        for (c[1] = 0; c[1] < id[1]; c[1]++)
            for (c[2] = 0; c[2] < id[2]; c[2]++)
                for (c[3] = 0; c[3] < id[3]; c[3]++) {
                    unsigned src = c[0] * is[0] + c[1] * is[1] + c[2] * is[2] + c[3] * is[3];
                    /* Output coord on axis k is the input coord of axis perm[k]. */
                    unsigned dst = c[p->perm[0]] * os[0] + c[p->perm[1]] * os[1] +
                                   c[p->perm[2]] * os[2] + c[p->perm[3]] * os[3];

                    const uint8_t *sp = in + (unsigned long)src * es;
                    uint8_t       *dp = out + (unsigned long)dst * es;
                    for (unsigned b = 0; b < es; b++)
                        dp[b] = sp[b];
                }
}


/* ------------------------------------------------------------------------- *
 * HW scaffold for perm [0,2,1,3]  (swap axes 1<->2).
 *
 * Emits one ops[108] task block per batch (d0 batches). Each block copies an
 * H x W plane of C2 contiguous subchannels and writes it transposed to a
 * W x H plane, using the recovered per-batch base addresses and the H<->W
 * swap in the DPU write cube. It rides the matmul register block (the same
 * ABC engine transpose uses in librknnrt -- emit_abc_regtask_matmul_v4).
 *
 * HW-VERIFY-PENDING. What is CERTAIN (recovered statically):
 *   - HW transpose == matmul-family COPY (no MAC, no weights).
 *   - perm [0,2,1,3] only; C2 (=d3) must align to the subchannel width.
 *   - the axis swap is exactly the swap of the two middle-dim strides between
 *     the read and the write base addressing (see header formula).
 * What is UNVERIFIED (behind chip-specific stubbed vtable setters):
 *   - the datamove copy-mode config bits (MAC/weight-load bypass) that
 *     RKNPUExecutor_setup_register_base + the chip vtable would set. The
 *     matmul serializer (gen_matmul_task) always emits the MAC/weight-load
 *     stream, so the descriptor below marks weights/DPU-bypass conservatively
 *     and the surf/line strides that carry the swap are the fields most
 *     likely to need on-board correction.
 * ------------------------------------------------------------------------- */
int
gen_transpose_int8(transpose_params_t *p)
{
    /* HW datapath only exists for perm [0,2,1,3]. */
    if (p->perm[0] != 0 || p->perm[1] != 2 || p->perm[2] != 1 || p->perm[3] != 3)
        return -1;

    unsigned N  = p->dims[0];   /* batch (copied through unchanged)   */
    unsigned H  = p->dims[1];   /* axis 1 -> becomes output axis 2    */
    unsigned W  = p->dims[2];   /* axis 2 -> becomes output axis 1    */
    unsigned C2 = p->dims[3];   /* contiguous subchannel group        */

    if (N == 0 || H == 0 || W == 0 || C2 == 0)
        return -1;

    unsigned in_plane  = H * W * C2;    /* elements per input batch  */
    unsigned out_plane = W * H * C2;    /* elements per output batch */

    for (unsigned n = 0; n < N; n++) {
        npu_cna_desc  cna_desc;
        npu_core_desc core_desc;
        npu_dpu_desc  dpu_desc;
        int           surf_stride;

        /* --- CNA: read the H x W plane, C2 subchannels (row-major). ------- */
        cna_desc.conv_mode      = direct_convolution;
        cna_desc.in_precision   = precision_int8;
        cna_desc.proc_precision = precision_int8;
        cna_desc.kernel_groups  = 0;
        cna_desc.feature_grains = H + 1;
        cna_desc.conv_x_stride  = 1;
        cna_desc.conv_y_stride  = 1;

        cna_desc.datain_width    = W;
        cna_desc.datain_height   = H;
        cna_desc.datain_channel  = C2;
        cna_desc.dataout_width   = H;   /* transposed: out width  = H */
        cna_desc.dataout_height  = W;   /* transposed: out height = W */
        cna_desc.dataout_atomics = W * H;

        /* Copy has no real kernel; keep a 1x1xC2 "identity" footprint so the
         * weight-size fields stay well-formed. decompress_addr0 left 0: the
         * datamove copy-mode is expected to bypass the weight load (the one
         * bit that needs on-board confirmation). */
        cna_desc.weight_width            = 1;
        cna_desc.weight_height           = 1;
        cna_desc.weight_kernels          = C2;
        cna_desc.weight_bytes_per_kernel = C2;
        cna_desc.weight_bytes            = C2 * C2;

        cna_desc.weight_bank  = 1;
        cna_desc.data_bank    = NPU_CBUF_BANKS - 1;
        cna_desc.data_entries = (W * C2) / 64;
        if ((W * C2) % 64)
            cna_desc.data_entries++;

        cna_desc.data_sign  = 0x1;
        cna_desc.cvt_type   = 0x1;
        cna_desc.cvt_bypass = 0x1;
        cna_desc.cvt_scale0 = 0x1;
        cna_desc.cvt_scale1 = 0x1;
        cna_desc.cvt_scale2 = 0x1;
        cna_desc.cvt_scale3 = 0x1;
        cna_desc.fc_skip_en = 0;
        cna_desc.data_offset = 0;
        cna_desc.pad_left   = 0;
        cna_desc.pad_top    = 0;

        /* Per-batch READ base (unchanged batch stride). */
        cna_desc.feature_base_addr = p->input_dma + n * in_plane;
        cna_desc.weight_offset     = 0;
        cna_desc.weight_burst_len  = 0xf;
        cna_desc.data_burst_len    = 0xf;

        /* Read strides walk the H x W plane row-major (C2 innermost). These
         * are the SWAP-BEARING read fields: reading advances one W-step per
         * line. (line/surf packing follows the NC1HWC2 /4 convention used by
         * conv2d; verify against C2 on-board.) */
        cna_desc.line_stride = W * 4;
        surf_stride          = cna_desc.line_stride * ((int)(H / 4) - 1);
        cna_desc.surf_stride = surf_stride < 0 ? surf_stride + 1 : surf_stride;
        cna_desc.dma_width   = W;
        cna_desc.dma_height  = H;
        cna_desc.dma_channel = C2;
        cna_desc.decompress_addr0 = 0;   /* no weights in a copy */

        /* --- CORE: output geometry is the transposed plane. --------------- */
        core_desc.proc_precision  = precision_int8;
        core_desc.qd_en           = 0;
        core_desc.dataout_height  = W - 1;   /* transposed */
        core_desc.dataout_width   = H - 1;   /* transposed */
        core_desc.dataout_channel = C2 - 1;

        /* --- DPU: write the transposed W x H plane. ----------------------- */
        dpu_desc.burst_len      = 0xf;
        dpu_desc.conv_mode      = direct_convolution;
        dpu_desc.output_mode    = 0x2;
        dpu_desc.flying_mode    = 0x0;
        /* int8 -> int8 straight copy (no requant), unlike matmul's int32 out. */
        dpu_desc.out_precision  = precision_int8;
        dpu_desc.in_precision   = precision_int8;
        dpu_desc.proc_precision = precision_int8;

        /* Per-batch WRITE base. The H<->W swap lives in the write cube dims
         * and dst_surf_stride below: output row (index over W) steps by H*C2,
         * i.e. the two middle strides are swapped vs the read. */
        dpu_desc.dst_base_addr   = p->output_dma + n * out_plane;
        dpu_desc.dst_surf_stride = H;              /* transposed row stride */
        dpu_desc.width           = H - 1;          /* swapped */
        dpu_desc.height          = W - 1;          /* swapped */
        dpu_desc.channel         = C2 - 1;

        dpu_desc.bs_bypass        = 1;
        dpu_desc.bs_alu_bypass    = 1;
        dpu_desc.bs_mul_bypass    = 1;
        dpu_desc.bs_relu_bypass   = 1;
        dpu_desc.bn_bypass        = 1;
        dpu_desc.bn_alu_bypass    = 1;
        dpu_desc.bn_mul_bypass    = 1;
        dpu_desc.bn_relu_bypass   = 1;
        dpu_desc.ew_bypass        = 1;
        dpu_desc.ew_op_bypass     = 1;
        dpu_desc.ew_lut_bypass    = 1;
        dpu_desc.ew_op_cvt_bypass = 1;
        dpu_desc.ew_relu_bypass   = 1;
        dpu_desc.fp32tofp16_en    = 0;
        dpu_desc.out_cvt_scale    = 1;
        dpu_desc.out_cvt_offset   = 0;
        dpu_desc.out_cvt_shift    = 0;
        dpu_desc.size_e_2         = 7;
        dpu_desc.size_e_1         = 7;
        dpu_desc.size_e_0         = 7;
        dpu_desc.od_bypass        = 1;
        dpu_desc.width_wdma       = H - 1;         /* swapped */
        dpu_desc.height_wdma      = W - 1;         /* swapped */
        dpu_desc.channel_wdma     = C2 - 1;
        dpu_desc.surf_add         = dpu_desc.dst_surf_stride * 4;

        gen_matmul_task(p->tasks + n * 108, &cna_desc, &core_desc, &dpu_desc);

        /* PC step size between task blocks (same fixup as gen_matmul_int8_tiled). */
        uint32_t regcfg_amount = 104;
        uint32_t data_amount   = (regcfg_amount + 4 + 2 - 1) / 2 - 1;  /* = 53 */
        p->tasks[n * 108 + 105] = NPUOP(OP_REG_PC, data_amount, PC_REGISTER_AMOUNTS);
    }

    p->num_tiles = N;
    return 0;
}


/* ------------------------------------------------------------------------- *
 * REAL on-NPU 2D transpose via an identity-activation matmul.
 *
 * The verified matmul computes out[m][n] = sum_k A[m][k]*B[n][k]. Feeding
 * B = X (N=P kernels of length K=Q) and A = identity_Q makes
 *   out[m][n] = sum_k (m==k)*X[n][k] = X[n][m] = Y[m][n],
 * i.e. Y = X^T, INT32, bit-exact (single non-zero term => no MAC overflow).
 * This rides the HW-verified gen_matmul_int8 path, so the transpose genuinely
 * runs on the NPU with no unverified copy-mode register bits. See header.
 * ------------------------------------------------------------------------- */
void
transpose2d_int8_ref(const int8_t *X, int32_t *Y, int P, int Q)
{
    /* Y[Q][P], Y[a][b] = X[b][a]. */
    for (int a = 0; a < Q; a++)
        for (int b = 0; b < P; b++)
            Y[a * P + b] = (int32_t)X[b * Q + a];
}

int
gen_transpose2d_int8(transpose2d_params_t *tp)
{
    int Pa = transpose2d_align_p((int)tp->p); /* N: multiple of 32 (mult-16 hangs HW) */
    int Qa = transpose2d_align_q((int)tp->q); /* M = K: multiple of 32 */

    matmul_params_t m = {
        .m           = (uint16_t)Qa, /* M = Qa  (identity rows / output rows) */
        .k           = (uint16_t)Qa, /* K = Qa  (contraction = identity size) */
        .n           = (uint16_t)Pa, /* N = Pa  (weight kernels = rows of X)  */
        .input_dma   = tp->input_dma,   /* identity activation */
        .weights_dma = tp->weights_dma, /* X as the weight kernels */
        .output_dma  = tp->output_dma,
        .tasks       = tp->tasks,
        .fp32tofp16  = 0,
    };

    return gen_matmul_int8(&m);
}

#include "yolo/yolo.h"
#include "npulib/npu_matmul.h"
#include "npu/rknpu.h"
#include "npu/rkconfig.h"
#include "mem/cache.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"
#include "npulib/npu_math.h"
#include "dev/t_timer.h"

/*
 * Conv2D on the NPU via im2col + tiled INT8 matmul, then integer bias-add,
 * dynamic requant, and (for YOP_CONV) SiLU — an exact port of
 * sim_fixedpoint.py::conv_silu / conv_linear and export_device.py::CModel.
 *
 * Layout convention: activation tensors are CHW int8 (row-major [c][h][w]),
 * exactly like the Python sim, so the arithmetic matches byte-for-byte.
 *
 * Matmul form:  acc[oc, M] = W[oc, K] @ cols[K, M]
 *   K = in_c*kh*kw (patch), M = out_h*out_w (spatial), N = out_c.
 * The NPU matmul computes out[m, n] = sum_k feat[m,k]*wt[n,k]; we feed
 * feat = cols^T (so feat[m,k] = patch element k of output pixel m) and
 * wt = weights[n,k]. Result out32[m, n] == acc[n, m].
 */

#define SHIFT YOLO_SCALE_SHIFT
#define S8(p, i) ((int32_t)(signed char)(p)[(i)])

extern int gen_matmul_int8_tiled(matmul_params_t *params);

/* coarse phase profiling (ticks), reported by yolo_forward */
uint64_t g_prof_im2col, g_prof_submit, g_prof_readback;

static uint32_t
dma_addr(void *p)
{
    return (uint32_t)((uint64_t)p & 0xFFFFFFFFu);
}

/* M-block size for conv: each block becomes NPU matmul submits whose per-tile
 * params->m stays <= the tiled-matmul cap. Larger blocks amortize submit
 * overhead; 1024 fits the DMA scratch and keeps tile counts low. */
#define YOLO_M_BLOCK 1024

/*
 * Run one tiled INT8 matmul: feat (already NC1HWC2 in m->dma_input),
 * resident weight in op->dma_w, output INT32 NC1HWC2 into m->dma_output.
 * M rows, K cols, N kernels. Returns 0 on success.
 */
static int
run_matmul(yolo_model_t *m, int M, int K, int N, int8_t *wbuf)
{
    matmul_params_t params = {
        .m = (uint16_t)M, .k = (uint16_t)K, .n = (uint16_t)N,
        .input_dma   = dma_addr(m->dma_input),
        .weights_dma = dma_addr(wbuf),
        .output_dma  = dma_addr(m->dma_output),
        .tasks       = m->dma_regs,
        .fp32tofp16  = 0,
        .num_tiles   = 0,
    };
    int ret = gen_matmul_int8_tiled(&params);
    if (ret != 0) {
        logger_error("YOLO: matmul gen failed (%d) M=%d K=%d N=%d\n", ret, M, K, N);
        return ret;
    }
    int tiles = params.num_tiles;
    if (tiles > YOLO_MAX_TILES) {
        logger_error("YOLO: too many tiles %d\n", tiles);
        return -10;
    }

    memcpy(m->dma_regcmd, m->dma_regs, tiles * 108 * sizeof(uint64_t));

    npu_task_t *tasks = (npu_task_t *)m->dma_tasks;
    npu_submit_t submit = {
        .flags           = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG,
        .timeout         = 10000,
        .task_start      = 0,
        .task_number     = 1,
        .task_counter    = 0,
        .priority        = 0,
        .task_obj_addr   = (uint64_t)tasks,
        .regcfg_obj_addr = 0,
        .task_base_addr  = 0,
        .user_data       = 0,
        .core_mask       = 0x1,
        .fence_fd        = -1,
    };

    /* flush inputs the NPU reads (weights were cleaned once at prelayout) */
    uint32_t feat_bytes = ((K + 15) / 16) * 16 * M;
    uint32_t out_bytes  = ((N + 3) / 4) * 4 * M * 4;
    clean_dcache_va_range(m->dma_input, feat_bytes);
    clean_dcache_va_range(m->dma_regcmd, tiles * 108 * sizeof(uint64_t));
    clean_dcache_va_range(m->dma_output, out_bytes);

    /* submit each tile's 108-op block one-by-one (see test_tile_matmul.c) */
    uint64_t ts = timer_get_system_ticks();
    for (int t = 0; t < tiles; t++) {
        tasks[0].flags         = 0;
        tasks[0].op_idx        = 0;
        tasks[0].enable_mask   = 0xd;
        tasks[0].int_mask      = 0x300;
        tasks[0].int_clear     = INT_CLEAR_VALUE;
        tasks[0].int_status    = 0;
        tasks[0].regcfg_amount = 104;
        tasks[0].regcfg_offset = 0;
        tasks[0].regcmd_addr   = (uint64_t)m->dma_regcmd + t * 108 * sizeof(uint64_t);
        submit.task_start  = 0;
        submit.task_number = 1;
        submit.subcore_task[0] = (npu_subcore_task_t){.task_start = 0, .task_number = 1};
        submit.subcore_task[1] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
        submit.subcore_task[2] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
        submit.subcore_task[3] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};
        submit.subcore_task[4] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};
        clean_dcache_va_range(m->dma_tasks, sizeof(npu_task_t));
        rknpu_submit_task(&submit);
    }
    invalidate_dcache_va_range(m->dma_output, out_bytes);
    g_prof_submit += timer_get_system_ticks() - ts;
    return 0;
}

/*
 * yolo_conv_npu — im2col the CHW input, run the NPU matmul, and scatter the
 * INT32 result into out32 laid out [oc][oh*ow] (i.e. acc[oc, m]).
 */
int
yolo_conv_npu(yolo_model_t *m, const yolo_op_t *op,
              const yolo_tensor_t *in, int32_t *out32, int *out_h, int *out_w)
{
    int C = in->c, H = in->h, W = in->w;
    int kh = op->kh, kw = op->kw;
    int stride = op->stride, pad = op->pad;
    int oh = (H + 2 * pad - kh) / stride + 1;
    int ow = (W + 2 * pad - kw) / stride + 1;
    int N = op->out_c;
    int K = C * kh * kw;
    int M = oh * ow;
    /* Pad K,N to multiples of 32: the INT8 NPU datapath tiles channels in
     * 32-groups; non-aligned K/N corrupt the descriptor geometry and hang the
     * NPU. Padded feature/weight lanes are zero (no effect on the dot product);
     * padded output kernels are simply not read back. The prelaid weight buffer
     * (op->dma_w) was already built with these padded dims. */
    int Kp = ((K + 31) / 32) * 32;
    int Np = ((N + 31) / 32) * 32;
    *out_h = oh;
    *out_w = ow;

    /* Process M (=oh*ow) in blocks so each NPU submit stays inside the only
     * geometry proven on this silicon (test_tile_matmul: params->m=256,
     * tile_m=64). Large single-shot matmuls (params->m in the thousands,
     * tile_m~1000) hang the NPU (int_raw=0xc0000045), so we never issue them. */
    static int kbase[YOLO_MAX_K];   /* per-k NC1HWC2 base offset (C2=16) */
    static int nbase[512];          /* per-n readback base offset (C2=4) */

    for (int m0 = 0; m0 < M; m0 += YOLO_M_BLOCK) {
        int mb = (M - m0 > YOLO_M_BLOCK) ? YOLO_M_BLOCK : (M - m0);
        uint64_t ti = timer_get_system_ticks();

        /* im2col feat[mb][Kp] laid out NC1HWC2 (feat[row,k]) into dma_input.
         * feature_data(Kp,mb,1,16,k+1,row+1,1) == (k/16)*mb*16 + 16*row + k%16,
         * so precompute the k-part once and stride 16 over rows. Zero the
         * padded buffer first so padded K-lanes stay 0. */
        uint32_t feat_bytes = ((Kp + 15) / 16) * 16 * mb;
        memset(m->dma_input, 0, feat_bytes);
        int8_t *feat = m->dma_input;
        for (int k = 0; k < K; k++)
            kbase[k] = (k >> 4) * mb * 16 + (k & 15);

        for (int row = 0; row < mb; row++) {
            int mm = m0 + row;
            int oy = mm / ow, ox = mm % ow;
            int base_y = oy * stride - pad;
            int base_x = ox * stride - pad;
            int row16 = row * 16;
            int k = 0;
            for (int c = 0; c < C; c++) {
                const int8_t *ch = in->data + (int64_t)c * H * W;
                for (int kr = 0; kr < kh; kr++) {
                    int iy = base_y + kr;
                    const int8_t *rowp = ch + iy * W;
                    int in_row = (iy >= 0 && iy < H);
                    for (int kc = 0; kc < kw; kc++, k++) {
                        int ix = base_x + kc;
                        int8_t v = 0;
                        if (in_row && ix >= 0 && ix < W)
                            v = rowp[ix];
                        feat[kbase[k] + row16] = v;
                    }
                }
            }
        }
        g_prof_im2col += timer_get_system_ticks() - ti;

        int ret = run_matmul(m, mb, Kp, Np, op->dma_w);
        if (ret != 0)
            return ret;

        /* read back INT32 NC1HWC2 (C2=4): dev[row,n] -> out32[n*M + (m0+row)].
         * feature_data(Np,mb,1,4,n+1,row+1,1) == (n/4)*mb*4 + 4*row + n%4.
         * output was generated with Np kernels; only the first N are real. */
        uint64_t tr = timer_get_system_ticks();
        int32_t *dev = (int32_t *)m->dma_output;
        for (int n = 0; n < N; n++)
            nbase[n] = (n >> 2) * mb * 4 + (n & 3);
        for (int n = 0; n < N; n++) {
            int32_t *outn = out32 + (int64_t)n * M + m0;
            int base = nbase[n];
            for (int row = 0; row < mb; row++)
                outn[row] = dev[base + row * 4];
        }
        g_prof_readback += timer_get_system_ticks() - tr;
    }
    return 0;
}

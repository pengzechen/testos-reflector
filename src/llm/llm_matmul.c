#include "llm/llm.h"
#include "npulib/npu_matmul.h"
#include "npu/rknpu.h"
#include "npu/rkconfig.h"
#include "mem/cache.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"

static uint32_t
get_dma_addr(void *addr)
{
    return (uint32_t)((uint64_t)addr & 0xFFFFFFFFu);
}

/*
 * llm_prelayout_weight — lay out a constant weight into NC1HWC2 once, into a
 * resident DMA buffer, and clean it from the cache. After this, per-token
 * matmul skips the (expensive, repeated) weight memset + layout + flush.
 * K = weight->cols, N = weight->rows.
 */
void
llm_prelayout_weight(llm_model_t *m, llm_weight_t *weight)
{
    (void)m;
    int N = (int)weight->rows;
    int K = (int)weight->cols;

    uint32_t K_pad = ((K + 31) / 32) * 32;
    uint32_t N_pad = ((N + 31) / 32) * 32;
    uint32_t wt_size = K_pad * N_pad;

    int8_t *wbuf = (int8_t *)t_mem_alloc(wt_size);
    if (!wbuf) {
        logger_error("LLM: prelayout OOM (N=%d K=%d, %u bytes)\n", N, K, wt_size);
        weight->dma_w = NULL;
        return;
    }
    memset(wbuf, 0, wt_size);   /* NC1HWC2 padding must be 0 */

    for (int n = 0; n < N; n++) {
        for (int k = 0; k < K; k++) {
            int dst = weight_int8(K, n + 1, k + 1);
            wbuf[dst] = weight->data[n * K + k];
        }
    }

    /* clean once — weight is immutable, NPU only ever reads it */
    clean_dcache_va_range(wbuf, wt_size);
    weight->dma_w = wbuf;
}

/*
 * llm_npu_matmul — INT8 matmul on NPU with INT32 output, then CPU dynamic
 * requantization to INT8 + a Q20 output scale.
 *
 * Architecture (verified in tools/sim_fixedpoint.py):
 *   Each activation tensor carries a running scale: real = int8 * scale / 2^20.
 *   NPU computes acc_int32 = W_int8 @ x_int8.
 *   Real output = acc * w_scale * in_scale / 2^40.
 *   Requant: out_int8 = round(acc * 127 / max|acc|).
 *   out_scale (Q20) = max|acc| * w_scale * in_scale / (127 * 2^20).
 *
 * Returns the Q20 output scale.  N = weight->rows, K = input dim.
 */
uint32_t
llm_npu_matmul(llm_model_t *m, int8_t *output,
               const int8_t *input, int M, int K,
               const llm_weight_t *weight, uint32_t in_scale,
               int32_t *logits32_out)
{
    int N = (int)weight->rows;

    /* Zero DMA buffers — NC1HWC2 padding must be 0 */
    uint32_t K_pad = ((K + 31) / 32) * 32;
    uint32_t N_pad = ((N + 31) / 32) * 32;
    uint32_t feat_size = ((K + 15) / 16) * 16 * M;
    uint32_t wt_size = K_pad * N_pad;
    /* INT32 output: C2=4, 4 bytes/elem */
    uint32_t out_elems = ((N + 3) / 4) * 4 * M;
    memset(m->dma_input, 0, feat_size);
    memset(m->dma_output, 0, out_elems * 4);

    /* layout input to NC1HWC2 feature format (C2=16 for INT8 input) */
    int8_t *feat = m->dma_input;
    for (int row = 0; row < M; row++) {
        for (int k = 0; k < K; k++) {
            int dst = feature_data(K, M, 1, 16, k + 1, row + 1, 1);
            feat[dst] = input[row * K + k];
        }
    }

    /* Weights: use the resident pre-laid-out buffer if available, else fall
     * back to laying them out into the shared scratch buffer this call. */
    int8_t *wbuf;
    if (weight->dma_w) {
        wbuf = weight->dma_w;
    } else {
        wbuf = m->dma_weights;
        memset(wbuf, 0, wt_size);
        for (int n = 0; n < N; n++) {
            for (int k = 0; k < K; k++) {
                int dst = weight_int8(K, n + 1, k + 1);
                wbuf[dst] = weight->data[n * K + k];
            }
        }
    }

    matmul_params_t params = {
        .m           = (uint16_t)M,
        .k           = (uint16_t)K,
        .n           = (uint16_t)N,
        .input_dma   = get_dma_addr(feat),
        .weights_dma = get_dma_addr(wbuf),
        .output_dma  = get_dma_addr(m->dma_output),
        .tasks       = m->dma_regs,
    };

    int ret = gen_matmul_int8(&params);   /* INT32 output */
    if (ret != 0) {
        logger_error("LLM: matmul gen failed (%d), M=%d K=%d N=%d\n", ret, M, K, N);
        return 1;
    }

    memcpy(m->dma_regcmd, m->dma_regs, 112 * sizeof(uint64_t));

    npu_task_t *task_arr = (npu_task_t *)m->dma_tasks;
    npu_task_t task = {
        .flags         = 0,
        .op_idx        = 0,
        .enable_mask   = 0xd,
        .int_mask      = 0x300,
        .int_clear     = INT_CLEAR_VALUE,
        .int_status    = 0,
        .regcfg_amount = 112 - (RKNPU_PC_DATA_EXTRA_AMOUNT + 4),
        .regcfg_offset = 0,
        .regcmd_addr   = (uint64_t)m->dma_regcmd,
    };
    memcpy(&task_arr[0], &task, sizeof(npu_task_t));

    npu_submit_t submit = {
        .flags           = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG,
        .timeout         = 5000,
        .task_start      = 0,
        .task_number     = 1,
        .task_counter    = 0,
        .priority        = 0,
        .task_obj_addr   = (uint64_t)task_arr,
        .regcfg_obj_addr = 0,
        .task_base_addr  = 0,
        .user_data       = 0,
        .core_mask       = 0x1,
        .fence_fd        = -1,
    };
    submit.subcore_task[0] = (npu_subcore_task_t){.task_start = 0, .task_number = 1};
    submit.subcore_task[1] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
    submit.subcore_task[2] = (npu_subcore_task_t){.task_start = 2, .task_number = 0};
    submit.subcore_task[3] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};
    submit.subcore_task[4] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};

    /* Flush only the buffers the NPU will read, and invalidate only what it
     * writes. The old code flushed a fixed 16 MB window on every matmul —
     * ~64-300x more cache-line ops than needed, dominating per-token latency.
     *   clean:  input, weights, regcmd, tasks  (CPU -> NPU)
     *   inval:  output                          (NPU -> CPU)
     * Resident weights (weight->dma_w) were cleaned once at prelayout time and
     * are immutable, so only the scratch-buffer fallback needs cleaning here. */
    clean_dcache_va_range(m->dma_input,   feat_size);
    if (!weight->dma_w)
        clean_dcache_va_range(m->dma_weights, wt_size);
    clean_dcache_va_range(m->dma_regcmd,  112 * sizeof(uint64_t));
    clean_dcache_va_range(m->dma_tasks,   sizeof(npu_task_t));
    /* output buffer must be clean before submit (padding zeros written above)
     * and invalidated after so the CPU sees NPU-written results. */
    clean_dcache_va_range(m->dma_output,  out_elems * 4);
    rknpu_submit_task(&submit);
    invalidate_dcache_va_range(m->dma_output, out_elems * 4);

    /* read back INT32 output (NC1HWC2, C2=4), find max|acc| */
    int32_t *out32 = (int32_t *)m->dma_output;
    static int32_t acc[4096];   /* static: avoid huge stack frame (bare-metal) */
    int32_t maxabs = 1;
    for (int row = 0; row < M; row++) {
        for (int n = 0; n < N; n++) {
            int src = feature_data(N, M, 1, 4, n + 1, row + 1, 1);
            int32_t v = out32[src];
            acc[n] = v;
            int32_t a = v < 0 ? -v : v;
            if (a > maxabs) maxabs = a;
            /* expose raw INT32 logits (LM head: avoids int8 requant loss) */
            if (logits32_out) logits32_out[row * N + n] = v;
        }
        /* requant to int8: out = round(acc * 127 / maxabs) */
        for (int n = 0; n < N; n++) {
            int64_t num = (int64_t)acc[n] * 127 * 2;
            int64_t q;
            if (acc[n] >= 0) q = (num + maxabs) / ((int64_t)maxabs * 2);
            else             q = (num - maxabs) / ((int64_t)maxabs * 2);
            if (q > 127) q = 127;
            if (q < -128) q = -128;
            output[row * N + n] = (int8_t)q;
        }
    }

    /* output scale (Q20): out_s = maxabs * w_scale * in_scale / (127 * 2^20)
     * where w_scale, in_scale are Q20. Matches sim_fixedpoint.py mm().
     * maxabs~1e6, scales~1e4-1e6; product fits in uint64 (<1.8e19). */
    uint64_t prod = (uint64_t)maxabs * (uint64_t)weight->w_scale_q20
                    * (uint64_t)in_scale;
    uint64_t out_s = prod / ((uint64_t)127 << LLM_SCALE_SHIFT);
    if (out_s < 1) out_s = 1;
    return (uint32_t)out_s;
}

/*
 * test_transpose.c -- Transpose tests for the RK3588 NPU.
 *
 * Two parts:
 *   1) CPU sanity for the general 4D transpose (perm [0,2,1,3]) using an
 *      independent index reference -- validates transpose_int8()'s stride math.
 *   2) REAL on-NPU 2D transpose Y = X^T, lowered to an identity-activation
 *      matmul (gen_transpose2d_int8 -> gen_matmul_int8). Because the lowering
 *      is mathematically exact -- out[m][n] = sum_k I[m][k]*X[n][k] = X[n][m] --
 *      the INT32 NPU output is compared bit-exact against the CPU golden
 *      (transpose2d_int8_ref). This is a genuine HW test: t_mem_alloc /
 *      rknpu_get_dma_addr / npu_submit_t / dcache clean+invalidate /
 *      rknpu_submit_task, mirroring test_matmul.c / test_deconv.c.
 */

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "mem/t_mem.h"
#include "mem/cache.h"

#include "npulib/npu_matmul.h"     /* feature_data(), weight_int8() */
#include "npulib/npu_transpose.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"

uint32_t rknpu_get_dma_addr(void *addr);

/* ---- Part 1: CPU 4D sanity ------------------------------------------------ */
#define D0 2
#define D1 3
#define D2 4
#define D3 2
#define TOTAL (D0 * D1 * D2 * D3)

static int8_t cpu_in[TOTAL];
static int8_t cpu_out[TOTAL];

/* ---- Part 2: HW 2D transpose ---------------------------------------------- */
/* X is [P][Q]; Y = X^T is [Q][P]. The lowering pads P and Q up to a multiple
 * of 32 (N=PA and K=QA must both be mult-32 or the NPU descriptor geometry is
 * corrupted and the job hangs -- see npu_transpose.h). Here Q=32 is already
 * aligned; P=16 pads up to PA=32. */
#define P 16
#define Q 32
#define PA ((P + 31) & ~31) /* = 32 (N: mult-32; a half-tile N=16 hangs HW) */
#define QA ((Q + 31) & ~31) /* = 32 */

static int8_t  X_data[P * Q];
static int32_t Y_golden[Q * P];
static uint64_t npu_regs[112];

static int8_t
rand_int8(void)
{
    int8_t val = ((rand_tick() % 255) - 128);
    if (val > 127) val -= 128;
    if (val < -128) val += 128;
    return val;
}

static void
transpose_cpu_sanity(void)
{
    logger_info("=== Transpose INT8 CPU sanity (perm 0,2,1,3) ===\n");
    logger_info("Shape: [%d,%d,%d,%d] -> [%d,%d,%d,%d]\n", D0, D1, D2, D3, D0, D2, D1, D3);

    for (int i = 0; i < TOTAL; i++)
        cpu_in[i] = (int8_t)(rand_tick() & 0xFF);

    transpose_params_t p = {
        .dims      = { D0, D1, D2, D3 },
        .perm      = { 0, 2, 1, 3 },
        .elem_size = 1,
        .input     = cpu_in,
        .output    = cpu_out,
    };
    transpose_int8(&p);

    int mismatches = 0;
    for (int a = 0; a < D0; a++)
        for (int b = 0; b < D1; b++)
            for (int c = 0; c < D2; c++)
                for (int d = 0; d < D3; d++) {
                    int src = ((a * D1 + b) * D2 + c) * D3 + d;
                    int dst = ((a * D2 + c) * D1 + b) * D3 + d; /* dims [D0,D2,D1,D3] */
                    if (cpu_out[dst] != cpu_in[src]) {
                        if (mismatches < 10)
                            logger_info("MISMATCH src=%d dst=%d: in=%d out=%d\n",
                                        src, dst, cpu_in[src], cpu_out[dst]);
                        mismatches++;
                    }
                }

    if (mismatches == 0)
        logger_info("Transpose CPU sanity: ALL PASSED (%d elements)\n", TOTAL);
    else
        logger_error("Transpose CPU sanity: %d mismatches out of %d\n", mismatches, TOTAL);
}

void
rknpu_test_transpose(void)
{
    srand_tick();

    /* ---- Part 1 --------------------------------------------------------- */
    transpose_cpu_sanity();

    /* ---- Part 2: real NPU 2D transpose ---------------------------------- */
    logger_info("=== Transpose INT8 NPU Test (2D, X[%d,%d] -> Y[%d,%d]) ===\n", P, Q, Q, P);
    logger_info("Lowered to identity-activation matmul: M=K=%d, N=%d\n", QA, PA);

    void *regcmd  = t_mem_alloc(1024);
    npu_task_t *tasks = t_mem_alloc(1024);
    void *input   = t_mem_alloc(QA * QA * sizeof(int8_t));  /* identity activation */
    void *weights = t_mem_alloc(PA * QA * sizeof(int8_t));  /* X as weight kernels */
    void *output  = t_mem_alloc(QA * PA * sizeof(int32_t)); /* INT32 result        */

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("Transpose Test: Memory allocation failed\n");
        return;
    }

    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    /* Random X[P][Q] and its CPU golden transpose Y[Q][P]. */
    for (int i = 0; i < P * Q; i++)
        X_data[i] = rand_int8();
    transpose2d_int8_ref(X_data, Y_golden, P, Q);

    /* Identity activation: A[m][k] = (m==k), laid out as feature_data(QA,QA,1,16,..). */
    int8_t *act = (int8_t *)input;
    memset(act, 0, QA * QA * sizeof(int8_t));
    for (int i = 0; i < QA; i++)
        act[feature_data(QA, QA, 1, 16, i + 1, i + 1, 1)] = 1;

    /* Weights = X as N=PA kernels of length K=QA (weight_int8), zero-padded. */
    int8_t *wt = (int8_t *)weights;
    memset(wt, 0, PA * QA * sizeof(int8_t));
    for (int n = 0; n < P; n++)
        for (int k = 0; k < Q; k++)
            wt[weight_int8(QA, n + 1, k + 1)] = X_data[n * Q + k];

    transpose2d_params_t tp = {
        .p           = P,
        .q           = Q,
        .input_dma   = input_dma,
        .weights_dma = weights_dma,
        .output_dma  = output_dma,
        .tasks       = npu_regs,
    };

    if (gen_transpose2d_int8(&tp) != 0) {
        logger_error("Transpose Test: gen_transpose2d_int8 failed\n");
        return;
    }

    memcpy(regcmd, npu_regs, sizeof(npu_regs));

    npu_task_t task = {
        .flags         = 0,
        .op_idx        = 0,
        .enable_mask   = 0xd,
        .int_mask      = 0x300,
        .int_clear     = INT_CLEAR_VALUE,
        .int_status    = 0,
        .regcfg_amount = sizeof(npu_regs) / sizeof(uint64_t) - (RKNPU_PC_DATA_EXTRA_AMOUNT + 4),
        .regcfg_offset = 0,
        .regcmd_addr   = (uint64_t)regcmd,
    };
    memcpy(&tasks[0], &task, sizeof(npu_task_t));

    memset(output, 0, QA * PA * sizeof(int32_t));

    npu_submit_t submit = {
        .flags           = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG,
        .timeout         = 5000,
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
    submit.subcore_task[0] = (npu_subcore_task_t){.task_start = 0, .task_number = 1};
    submit.subcore_task[1] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
    submit.subcore_task[2] = (npu_subcore_task_t){.task_start = 2, .task_number = 0};
    submit.subcore_task[3] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};
    submit.subcore_task[4] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};

    clean_dcache_va_range(regcmd, 1024 * 1024 * 16);

    logger_info("Submitting transpose task to NPU...\n");
    rknpu_submit_task(&submit);

    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);
    logger_info("NPU transpose completed.\n");

    /* Verify: out[m=a][n=b] = X[b][a] = Y[a][b], read at feature_data(PA,QA,1,4,..). */
    int mismatches = 0;
    int32_t *out = (int32_t *)output;
    for (int a = 0; a < Q; a++)
        for (int b = 0; b < P; b++) {
            int32_t actual   = out[feature_data(PA, QA, 1, 4, b + 1, a + 1, 1)];
            int32_t expected = Y_golden[a * P + b];
            if (actual != expected) {
                if (mismatches < 10)
                    logger_info("MISMATCH a=%d b=%d: expected=%d actual=%d\n",
                                a, b, expected, actual);
                mismatches++;
            }
        }

    if (mismatches == 0)
        logger_info("Transpose NPU 2D [%d,%d]->[%d,%d]: ALL PASSED (%d elements)\n",
                    P, Q, Q, P, Q * P);
    else
        logger_error("Transpose NPU 2D: %d mismatches out of %d\n", mismatches, Q * P);
}

/*
 * test_tile_matmul.c — Tiled Matmul NPU hardware test for RK3588
 *
 * Tests INT8 matmul with M larger than single-tile CBUF capacity.
 * Verifies that multi-tile submission produces correct results.
 */

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "mem/t_mem.h"
#include "npulib/npu_matmul.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"
#include "dev/t_timer.h"
#include "mem/cache.h"

#define M    256
#define K    4096
#define N    32

#define MAX_TILES 16

static int8_t  matA[M * K];
static int8_t  matB[N * K];
static int32_t expected[M * N];
static uint64_t npu_regs[108 * MAX_TILES];

uint32_t rknpu_get_dma_addr(void *addr);

static int8_t
rand_int8(void)
{
    int8_t val = ((rand_tick() % 255) - 128);
    if (val > 127) val -= 128;
    if (val < -128) val += 128;
    return val;
}

static void
matmul_cpu_reference(void)
{
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            int64_t sum = 0;
            for (int l = 0; l < K; l++)
                sum += (int32_t)(matA[i * K + l] * matB[j * K + l]);
            expected[i * N + j] = (int32_t)sum;
        }
    }
}

void
rknpu_test_tile_matmul(void)
{
    logger_info("=== Tiled Matmul NPU Test ===\n");
    logger_info("M=%d, K=%d, N=%d\n", M, K, N);

    void *regcmd  = t_mem_alloc(108 * MAX_TILES * sizeof(uint64_t));
    npu_task_t *tasks = t_mem_alloc(MAX_TILES * sizeof(npu_task_t));
    void *input   = t_mem_alloc(M * K * sizeof(int8_t));
    void *weights = t_mem_alloc(N * K * sizeof(int8_t));
    void *output  = t_mem_alloc(M * N * sizeof(int32_t));

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("Tile Matmul Test: Memory allocation failed\n");
        return;
    }

    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    matmul_params_t params = {
        .m = M, .k = K, .n = N,
        .input_dma   = input_dma,
        .weights_dma = weights_dma,
        .output_dma  = output_dma,
        .tasks       = npu_regs,
        .fp32tofp16  = 0,
        .num_tiles   = 0,
    };

    int ret = gen_matmul_int8_tiled(&params);
    if (ret != 0) {
        logger_error("Tile Matmul Test: gen_matmul_int8_tiled failed (%d)\n", ret);
        return;
    }

    logger_info("Tiled matmul: %d tiles generated\n", params.num_tiles);

    if (params.num_tiles > MAX_TILES) {
        logger_error("Tile Matmul Test: too many tiles (%d > %d)\n",
                     params.num_tiles, MAX_TILES);
        return;
    }

    memcpy(regcmd, npu_regs, params.num_tiles * 108 * sizeof(uint64_t));

    memset(input, 0, M * K);
    memset(weights, 0, N * K);
    memset(output, 0, M * N * sizeof(int32_t));

    srand_tick();
    for (int i = 0; i < M * K; i++)
        matA[i] = rand_int8();
    for (int i = 0; i < N * K; i++)
        matB[i] = rand_int8();

    logger_info("Computing CPU reference...\n");
    uint32_t t0 = timer_get_system_ticks();
    matmul_cpu_reference();
    uint32_t t1 = timer_get_system_ticks();
    logger_info("CPU reference done (%d ticks)\n", t1 - t0);

    /* Layout input to NC1HWC2 (C2=16) */
    int8_t *feat = (int8_t *)input;
    for (int m = 0; m < M; m++) {
        for (int k = 0; k < K; k++) {
            int src = m * K + k;
            int dst = feature_data(K, M, 1, 16, k + 1, m + 1, 1);
            feat[dst] = matA[src];
        }
    }

    /* Layout weights using weight_int8 tiling */
    int8_t *wt = (int8_t *)weights;
    for (int n = 0; n < N; n++) {
        for (int k = 0; k < K; k++) {
            int src = n * K + k;
            int dst = weight_int8(K, n + 1, k + 1);
            wt[dst] = matB[src];
        }
    }

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
    submit.subcore_task[0] = (npu_subcore_task_t){.task_start = 0, .task_number = 1};
    submit.subcore_task[1] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
    submit.subcore_task[2] = (npu_subcore_task_t){.task_start = 1, .task_number = 0};
    submit.subcore_task[3] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};
    submit.subcore_task[4] = (npu_subcore_task_t){.task_start = 0, .task_number = 0};

    clean_dcache_va_range(regcmd, 1024 * 1024 * 16);

    logger_info("Submitting tiled matmul (%d tiles, one-by-one) to NPU...\n", params.num_tiles);
    t0 = timer_get_system_ticks();

    for (int t = 0; t < params.num_tiles; t++) {
        tasks[0].flags         = 0;
        tasks[0].op_idx        = 0;
        tasks[0].enable_mask   = 0xd;
        tasks[0].int_mask      = 0x300;
        tasks[0].int_clear     = INT_CLEAR_VALUE;
        tasks[0].int_status    = 0;
        tasks[0].regcfg_amount = 104;
        tasks[0].regcfg_offset = 0;
        tasks[0].regcmd_addr   = (uint64_t)regcmd + t * 108 * sizeof(uint64_t);
        submit.task_start = 0;
        submit.task_number = 1;
        submit.subcore_task[0] = (npu_subcore_task_t){.task_start = 0, .task_number = 1};
        rknpu_submit_task(&submit);
    }

    t1 = timer_get_system_ticks();

    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);
    logger_info("NPU tiled matmul completed (%d ticks)\n", t1 - t0);

    /* Verify output */
    int mismatches = 0;
    int32_t *out = (int32_t *)output;
    for (int m = 1; m <= M; m++) {
        for (int n = 1; n <= N; n++) {
            int32_t actual   = out[feature_data(N, M, 1, 4, n, m, 1)];
            int32_t exp      = expected[(m - 1) * N + (n - 1)];
            if (actual != exp) {
                if (mismatches < 10)
                    logger_info("MISMATCH m=%d n=%d: expected=%d actual=%d\n",
                                m, n, exp, actual);
                mismatches++;
            }
        }
    }

    if (mismatches == 0) {
        logger_info("Tiled Matmul [%d,%d] x [%d,%d] (%d tiles): ALL PASSED\n",
                    M, K, K, N, params.num_tiles);
    } else {
        logger_error("Tiled Matmul: %d mismatches out of %d\n",
                     mismatches, M * N);
    }
}

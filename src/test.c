

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "npu/rkmem.h"
#include "npulib/npu_matmul.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"

#define M 4
#define K 8
#define N 4

// matrix buffers
static int8_t   matrixA[M * K];
static int8_t   matrixB[N * K];
static int32_t  expected_result[M * N];
static uint64_t npu_regs[112];

// ======================================================
// 工具函数
// ======================================================
uint32_t
rknpu_get_dma_addr(void *addr)
{
    uint64_t ptr_val = (uint64_t) addr;

    if (ptr_val >> 32) {
        logger_warn("rknpu_get_dma_addr: address %p exceeds 32-bit range (high=0x%lx)",
                    addr,
                    (unsigned long) (ptr_val >> 32));
    }
    return (uint32_t) (ptr_val & 0xFFFFFFFFu);
}

static void
log_matrix_int8(const char *name, int8_t *mat, int rows, int cols)
{
    logger_info("%s (%dx%d):\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        char  buf[512] = {0};
        char *p        = buf;
        for (int j = 0; j < cols; j++) {
            p += my_snprintf(p, sizeof(buf) - (p - buf), "%4d ", mat[i * cols + j]);
        }
        logger_info("%s\n", buf);  // 每行单独打印
    }
}

static void
log_matrix_int32(const char *name, int32_t *mat, int rows, int cols)
{
    logger_info("%s (%dx%d):\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        char  buf[1024] = {0};
        char *p         = buf;
        for (int j = 0; j < cols; j++) {
            p += my_snprintf(p, sizeof(buf) - (p - buf), "%6d ", mat[i * cols + j]);
        }
        logger_info("%s\n", buf);  // 每行单独打印
    }
}

// ======================================================
// 软件矩阵计算（用于期望结果）
// ======================================================
static void
matmul_int(int m, int k, int n, const int8_t *src0, const int8_t *src1, int32_t *dst)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            int32_t sum = 0;
            for (int l = 0; l < k; l++) {
                sum += (int32_t) (src0[i * k + l]) * (int32_t) (src1[j * k + l]);
            }
            dst[i * n + j] = sum;
        }
    }
}

// ======================================================
// 数据准备（随机矩阵生成 + 内存布局转换）
// ======================================================
static void
prepare_test_data(void *input, void *weights)
{
    memset(input, 0, M * K * sizeof(int8_t));
    memset(weights, 0, N * K * sizeof(int8_t));
    memset(expected_result, 0, sizeof(expected_result));

    srand_tick();

    for (int i = 0; i < M * K; i++)
        matrixA[i] = (int8_t) rand_tick();
    for (int i = 0; i < N * K; i++)
        matrixB[i] = (int8_t) rand_tick();

    int8_t *feature_data_int8 = (int8_t *) input;
    int8_t *weights_int8      = (int8_t *) weights;

    // 转换权重布局
    for (int n = 1; n <= N; n++)
        for (int k = 1; k <= K; k++)
            weights_int8[weight_int8(K, n, k)] = matrixB[(n - 1) * K + (k - 1)];

    // 转换输入布局
    for (int m = 1; m <= M; m++)
        for (int k = 1; k <= K; k++)
            feature_data_int8[feature_data(K, M, 1, 16, k, m, 1)] = matrixA[(m - 1) * K + (k - 1)];

    // 计算期望结果
    matmul_int(M, K, N, (int8_t *) &matrixA, (int8_t *) &matrixB, (int32_t *) &expected_result);

    log_matrix_int8("Matrix A", matrixA, M, K);
    log_matrix_int8("Matrix B", matrixB, N, K);

    log_matrix_int32("Expected Result", expected_result, M, N);
}

// ======================================================
// 主测试函数
// ======================================================
void
rknpu_test(void)
{
    void       *regcmd  = rkmem_alloc(1024);  // 8 * 112 = 896 bytes
    npu_task_t *tasks   = rkmem_alloc(sizeof(npu_task_t) * 10);
    void       *input   = rkmem_alloc(M * K * sizeof(int8_t));
    void       *weights = rkmem_alloc(N * K * sizeof(int8_t));
    void       *output  = rkmem_alloc(M * N * sizeof(int32_t));

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("RKNPU Test: Memory allocation failed\n");
        return;
    }

    memset(output, 0, M * N * sizeof(int32_t));

    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    matmul_params_t params = {
        .m           = M,
        .k           = K,
        .n           = N,
        .input_dma   = input_dma,
        .weights_dma = weights_dma,
        .output_dma  = output_dma,
        .tasks       = (uint64_t *) &npu_regs,
    };

    if (gen_matmul_int8(&params) != 0) {
        logger_error("RKNPU Test: gen_matmul_int8 failed\n");
        return;
    }

    memcpy(regcmd, npu_regs, sizeof(npu_regs));

    npu_task_t task = {
        .flags         = 0,
        .op_idx        = 0,
        .enable_mask   = 0xd,
        .int_mask      = 0x300,  // wait for DPU to finish
        .int_clear     = INT_CLEAR_VALUE,
        .int_status    = 0,
        .regcfg_amount = sizeof(npu_regs) / sizeof(uint64_t) - (RKNPU_PC_DATA_EXTRA_AMOUNT + 4),
        .regcfg_offset = 0,
        .regcmd_addr   = (uint64_t) regcmd,
    };
    memcpy(&tasks[0], &task, sizeof(npu_task_t));

    prepare_test_data(input, weights);

    npu_submit_t submit = {
        .flags           = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG,
        .timeout         = 5000,
        .task_start      = 0,
        .task_number     = 1,
        .task_counter    = 0,
        .priority        = 0,
        .task_obj_addr   = (uint64_t) tasks,
        .regcfg_obj_addr = 0,
        .task_base_addr  = (uint64_t) tasks,
        .user_data       = 0,
        .core_mask       = 0x1,
        .fence_fd        = -1,
    };

    submit.subcore_task[0] = (npu_subcore_task_t) {.task_start = 0, .task_number = 1};
    submit.subcore_task[1] = (npu_subcore_task_t) {.task_start = 1, .task_number = 0};
    submit.subcore_task[2] = (npu_subcore_task_t) {.task_start = 2, .task_number = 0};
    submit.subcore_task[3] = (npu_subcore_task_t) {.task_start = 0, .task_number = 0};
    submit.subcore_task[4] = (npu_subcore_task_t) {.task_start = 0, .task_number = 0};

    rknpu_submit_task(&submit);

    logger_warn("RkNPU submit task completed.\n");

    int      ret;
    int32_t *output_data = (int32_t *) output;
    for (int m = 1; m <= M; m++) {
        for (int n = 1; n < N; n++) {
            int32_t actual   = output_data[feature_data(N, M, 1, 4, n, m, 1)];
            int32_t expected = expected_result[((m - 1) * N) + (n - 1)];
            if (actual != expected) {
                logger_info("mismatch m:%d n:%d  expected:%d acutal:%d \n", m, n, expected, actual);
                ret = -1;
            }
        }
    }
    if (ret == 0) {
        logger_info("Multiplication of [%d,%d] x [%d,%d] succesful \n", M, K, N, K);
    }
}
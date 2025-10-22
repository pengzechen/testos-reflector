

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "npu/rkmem.h"
#include "npulib/npu_matmul.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"

#define MAX_M 544
#define MAX_K 4096
#define MAX_N 4096

// Test currently runs against kernel 5.10 haven't tested 6.1 kernel.

// matrix A max size
int8_t matrixA[(MAX_M * MAX_K)];

// matrix B max size
int8_t matrixB[(MAX_N * MAX_K)];

// matrix C max size
int32_t expected_result[MAX_M * MAX_N];

uint64_t npu_regs[112];

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
void
matmul_int(int m, int k, int n, int8_t *src0, int8_t *src1, int32_t *dst)
{
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) {
            // change float to int
            // float sum = 0;
            int64_t sum = 0;
            for (int l = 0; l < k; l++) {
                sum += (int32_t) (src0[i * k + l] * src1[j * k + l]);
            }
            dst[i * n + j] = sum;
        }
    }
}

int8_t
rand_int()
{
    return (int8_t) ((rand_tick() % 255) + 1);
}


// ======================================================
// 主测试函数
// ======================================================
void
rknpu_test(void)
{
    unsigned int M = 1;
    unsigned int K = 32;
    unsigned int N = 32;

    if ((M <= 0) || (M > MAX_M) | (((M % 4) != 0) && (M != 1))) {
        logger_error("M [%d] is out of range or not a mutliple of 4 \n", M);
        return;
    }

    if ((K <= 0) || (K > MAX_K) || ((K % 32) != 0)) {
        logger_error("K [%d] is out of range or not a mutliple of 32\n", K);
        return;
    }

    if ((N <= 0) || (N > MAX_N) || ((N % 16) != 0)) {
        logger_error("N [%d] is out of range or not a mutliple of 32\n", N);
        return;
    }


    void       *regcmd  = rkmem_alloc(1024);  // 8 * 112 = 896 bytes
    npu_task_t *tasks   = rkmem_alloc(1024);
    // void       *input   = (void *) 0xffff0000;  //rkmem_alloc(M * K * sizeof(int8_t));
    // void       *weights = (void *) 0xffff2000;  //rkmem_alloc(N * K * sizeof(int8_t));
    // void       *output  = (void *) 0xffff4000;  //rkmem_alloc(M * N * sizeof(int32_t));
    void       *input   = (void *) rkmem_alloc(M * K * sizeof(int8_t));
    void       *weights = (void *) rkmem_alloc(N * K * sizeof(int8_t));
    void       *output  = (void *) rkmem_alloc(M * N * sizeof(int32_t));
    
    uint32_t input_dma   = rknpu_get_dma_addr(input);
    uint32_t weights_dma = rknpu_get_dma_addr(weights);
    uint32_t output_dma  = rknpu_get_dma_addr(output);

    logger_info("input dma is %lx, output dma is %lx, weights dma is %lx\n",
           input_dma,
           output_dma,
           weights_dma);

    if (!regcmd || !tasks || !input || !weights || !output) {
        logger_error("RKNPU Test: Memory allocation failed\n");
        return;
    }

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


    memset((void *) input, 0, M * K * sizeof(int8_t));
    memset((void *) weights, 0, K * N * sizeof(int8_t));
    memset((void *) output, 0, M * N * sizeof(int32_t));


    srand_tick();

    for (int i = 0; i < M * K; i++) {
        matrixA[i] = rand_int();
    }

    for (int i = 0; i < N * K; i++) {
        matrixB[i] = rand_int();
    }

    int8_t *weights_int8 = weights;

    for (int n = 1; n <= N; n++) {
        for (int k = 1; k <= K; k++) {
            weights_int8[weight_int8(K, n, k)] = matrixB[((n - 1) * K) + (k - 1)];
        }
    }

    int8_t *feature_data_int8 = (int8_t *) input;

    for (int m = 1; m <= M; m++) {
        for (int k = 1; k <= K; k++) {
            feature_data_int8[feature_data(K, M, 1, 16, k, m, 1)] =
                matrixA[((m - 1) * K) + (k - 1)];
        }
    }

    matmul_int(M, K, N, (int8_t *) &matrixA, (int8_t *) &matrixB, (int32_t *) &expected_result);


    int ret0 = 0;
    int32_t *output_data = (int32_t *) output;
    for (int m = 1; m <= M; m++) {
        for (int n = 1; n < N; n++) {
            int32_t actual   = output_data[feature_data(N, M, 1, 4, n, m, 1)];
            int32_t expected = expected_result[((m - 1) * N) + (n - 1)];
            if (actual != expected) {
                logger_info("mismatch m:%d n:%d  expected:%d acutal:%d \n", m, n, expected, actual);
                ret0 = -1;
            }
        }
    }
    if (ret0 == 0) {
        logger_info("Multiplication of [%d,%d] x [%d,%d] succesful \n", M, K, N, K);
    }

    npu_submit_t submit = {
        .flags           = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG,
        .timeout         = 5000,
        .task_start      = 0,
        .task_number     = 1,
        .task_counter    = 0,
        .priority        = 0,
        .task_obj_addr   = (uint64_t) tasks,
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

    rknpu_submit_task(&submit);

    logger_warn("RkNPU submit task completed.\n");

    int      ret          = 0;
    int32_t *output_data0 = (int32_t *) output;
    for (int m = 1; m <= M; m++) {
        for (int n = 1; n <= N; n++) {
            int32_t actual   = output_data0[feature_data(N, M, 1, 4, n, m, 1)];
            int32_t expected = expected_result[((m - 1) * N) + (n - 1)];
            if (actual != expected) {
                logger_info("mismatch m:%d n:%d  expected:%x acutal:%x \n", m, n, expected, actual);
                ret = -1;
            }
        }
    }
    if (ret == 0) {
        logger_info("Multiplication of [%d,%d] x [%d,%d] succesful \n", M, K, N, K);
    }

    logger_info("except raw:\n");
    dump_reg((uint64_t*)expected_result, (M*N)/2);
    logger_info("actual raw:\n");
    dump_reg(output, (M*N)/2);
}
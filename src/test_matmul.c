

#include "npu/rkconfig.h"
#include "npu/rknpu.h"
#include "mem/t_mem.h"
#include "npulib/npu_matmul.h"

#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "lib/rand.h"
#include "lib/sort.h"
#include "dev/t_timer.h"

#include "mem/cache.h"

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

int weight_map[MAX_K * MAX_N];

int feature_map[MAX_M * MAX_K];

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

void
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

void
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
    int8_t val = ((rand_tick() % 255) - 128);
    if (val > 127) {
        val -= 128;
    }
    if (val < -128) {
        val += 128;
    }
    if (val > 127 || val < -128) {
        logger_error("rand int error %d\n", val);
        return 1;
    }
    return val;
}




// ======================================================
// 主测试函数
// ======================================================
void
rknpu_test_matmul(void)
{
    unsigned int M = 32;
    unsigned int K = 64;
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


    void       *regcmd  = t_mem_alloc(1024);  // 8 * 112 = 896 bytes
    npu_task_t *tasks   = t_mem_alloc(1024);
    void       *input   = (void *) t_mem_alloc(M * K * sizeof(int8_t));
    void       *weights = (void *) t_mem_alloc(N * K * sizeof(int8_t));
    void       *output  = (void *) t_mem_alloc(M * N * sizeof(int32_t));

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

    // --- 1. 初始化阶段，只做一次 ---
    // 生成 weight_map / feature_map（仅用于第一次重排）
    for (int n = 0; n < N; n++) {
        for (int k = 0; k < K; k++) {
            weight_map[n * K + k] = weight_int8(K, n + 1, k + 1);
        }
    }

    for (int m = 0; m < M; m++) {
        for (int k = 0; k < K; k++) {
            feature_map[m * K + k] = feature_data(K, M, 1, 16, k + 1, m + 1, 1);
        }
    }

    
    // --- 2. 第一次重排，生成按 NPU 内存布局的矩阵缓存 ---
    int8_t *matrixB_int8_layout = t_mem_alloc(K * N);  // 按 NPU 内存布局
    int8_t *matrixA_int8_layout = t_mem_alloc(M * K);

    // --- 3. 可选：CPU 软件模拟，用于验证 ---
    logger_warn("current tick (cpu compute before): %d\n", timer_get_system_ticks());
    matmul_int(M, K, N, (int8_t *) &matrixA, (int8_t *) &matrixB, (int32_t *) &expected_result);
    logger_warn("current tick (cpu compute after): %d\n", timer_get_system_ticks());


    logger("layout before: %d\n", timer_get_system_ticks());
    //  =================  优化这里 =====================
    // 第一版
    for (int i = 0; i < K * N; i++) {
        matrixB_int8_layout[weight_map[i]] = matrixB[i];
    }
    for (int i = 0; i < M * K; i++) {
        matrixA_int8_layout[feature_map[i]] = matrixA[i];
    }
    // =================================================
    logger("layout after: %d\n", timer_get_system_ticks());

    
    // --- 4. 更新矩阵数据时直接 memcpy ---
    int8_t *weights_int8 = weights;
    memcpy_neon((uint8_t *) weights_int8, (const uint8_t *) matrixB_int8_layout, K * N);

    int8_t *feature_data_int8 = (int8_t *) input;
    memcpy_neon((uint8_t *) feature_data_int8, (const uint8_t *) matrixA_int8_layout, M * K);

    logger_warn("current tick3: %d\n", timer_get_system_ticks());


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

    clean_dcache_va_range(regcmd, 1024 * 1024 * 16);

    rknpu_submit_task(&submit);

    invalidate_dcache_va_range(regcmd, 1024 * 1024 * 16);

    logger_warn("current tick4: %d\n", timer_get_system_ticks());


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
        logger_info("Multiplication of [%d,%d] x [%d,%d] succesful \n", M, K, K, N);
    }

    // 查看内存中的数值
    // logger_info("except raw:\n");
    // dump_reg((uint64_t *) expected_result, (M * N) / 2);
    // logger_info("actual raw:\n");
    // dump_reg(output, (M * N) / 2);
}


// 12,480 ms
// 40,060 ms

// 412 + 42 = 4,540 ms
//            9,980 ms

// 64,080 ms
// 00,100 ms


#include "t_types.h"
#include "cfg/t_cfg.h"
#include "lib/t_logger.h"
#include "t_timer.h"

// 3个外部接口

void
launch_on_core(int cpu_id, void (*entry)(int, void *), void *arg);
void
wake_all_cores(int num);
void
wait_all_cores(int num);

// =============================================================

#define REORDER_USE_CPUS T_SMP_NUM

typedef struct
{
    int8_t         *dst;
    const int8_t   *src;
    const uint32_t *map;
    size_t          start;
    size_t          end;
} reorder_task_t;

static reorder_task_t tasks[REORDER_USE_CPUS];

// 每个核执行的回调函数。
void
reorder_worker(int cpu_id, void *arg)
{
    reorder_task_t *task = (reorder_task_t *) arg;

    for (size_t i = task->start; i < task->end; i++) {
        task->dst[task->map[i]] = task->src[i];
    }
}

#define K     4096
#define N     4096
#define TOTAL (K * N)

static int8_t   matrixB[TOTAL];
static int8_t   matrixB_int8_layout[TOTAL];
static uint32_t weight_map[TOTAL];

void
prepare_data()
{
    // src 矩阵内容：每个元素 = i
    for (int i = 0; i < TOTAL; i++) {
        matrixB[i] = (int8_t) (i + 1);
    }

    // map：打乱顺序，比如逆序映射
    for (int i = 0; i < TOTAL; i++) {
        weight_map[i] = TOTAL - 1 - i;
    }

    // 清空目标缓冲区
    for (int i = 0; i < TOTAL; i++) {
        matrixB_int8_layout[i] = 0;
    }
}

bool
verify_reorder()
{
    bool ok = true;

    for (size_t i = 0; i < TOTAL; i++) {
        int8_t expected = matrixB[weight_map[i]];
        int8_t actual   = matrixB_int8_layout[i];

        if (expected != actual) {
            logger_error("mismatch at index %zu: expected %d, actual %d\n", i, expected, actual);
            ok = false;
        }
    }

    if (ok) {
        logger_info("verify success: all elements correct\n");
    } else {
        logger_warn("verify failed: some elements mismatch\n");
    }

    return ok;
}

void
reorder_matrix_multi_core(int8_t         *dst,
                          const int8_t   *src,
                          const uint32_t *map,
                          size_t          total,
                          int             num_cores)
{

    size_t chunk = (total + num_cores - 1) / num_cores;

    // 清空目标
    // for (size_t i = 0; i < total; i++)
    //     dst[i] = 0;

    // 启动 secondary 核
    for (int c = 1; c < num_cores; c++) {
        reorder_task_t *t = &tasks[c];
        t->dst            = dst;
        t->src            = src;
        t->map            = map;
        t->start          = c * chunk;
        t->end            = (c + 1) * chunk;
        if (t->end > total)
            t->end = total;

        launch_on_core(c, reorder_worker, t);
    }

    // 主核自己处理 chunk0
    reorder_task_t t0 = {dst, src, map, 0, chunk};
    reorder_worker(0, &t0);

    wake_all_cores(num_cores);
    wait_all_cores(num_cores);
}

void
reorder_test()
{
    size_t total     = K * N;
    int    num_cores = REORDER_USE_CPUS;
    size_t chunk     = (total + num_cores - 1) / num_cores;

    prepare_data();

    logger("layout before: %d\n", timer_get_system_ticks());

    // =========================
    // 单核执行
    reorder_task_t single_task = {matrixB_int8_layout, matrixB, weight_map, 0, total};
    reorder_worker(0, &single_task);

    logger("single core done: %d\n", timer_get_system_ticks());

    // 验证单核结果
    verify_reorder();

    // =========================
    // 清空目标缓冲区，准备多核
    for (int i = 0; i < TOTAL; i++)
        matrixB_int8_layout[i] = 0;

    logger("layout before multi-core: %d\n", timer_get_system_ticks());

    // 启动多核
    for (int c = 1; c < num_cores; c++) {
        reorder_task_t *t = &tasks[c];
        t->dst            = matrixB_int8_layout;
        t->src            = matrixB;
        t->map            = weight_map;
        t->start          = c * chunk;
        t->end            = (c + 1) * chunk;
        if (t->end > total)
            t->end = total;

        launch_on_core(c, reorder_worker, t);
    }

    // 主核自己做 core0 部分
    reorder_task_t t0 = {matrixB_int8_layout, matrixB, weight_map, 0, chunk};
    reorder_worker(0, &t0);

    wake_all_cores(num_cores);

    wait_all_cores(num_cores);

    logger("multi-core done: %d\n", timer_get_system_ticks());

    // 验证多核结果
    verify_reorder();
}

// smp = 4
// single 38,810 ms
// multi  19,690

// smp = 6
// single 39,070 ms
// multi  13,720 ms

// smp = 8
// single 38,840 ms
// multi  10,550 ms
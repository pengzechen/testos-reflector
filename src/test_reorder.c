

#include "t_types.h"
#include "cfg/t_cfg.h"
#include "lib/t_logger.h"
#include "t_timer.h"
#include "reorder.h"

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

void
reorder_worker_block(int cpu_id, void *arg)
{
    reorder_task_t *task       = (reorder_task_t *) arg;
    const size_t    block_size = 64;  // 一次处理 64 字节，Cache line 对齐

    for (size_t i = task->start; i < task->end; i += block_size) {
        size_t end = i + block_size;
        if (end > task->end)
            end = task->end;

        for (size_t j = i; j < end; j++) {
            task->dst[task->map[j]] = task->src[j];
        }
    }
}

void
reorder_worker_optimized(int cpu_id, void *arg)
{
    reorder_task_t *task = (reorder_task_t *) arg;
    const int8_t   *src  = task->src;
    int8_t         *dst  = task->dst;
    const uint32_t *map  = task->map;

    // --- 参数可调 ---
    const size_t PREFETCH_DIST = 64;   // 提前预取 64 个元素
    const size_t BLOCK         = 256;  // 每次处理 256 元素（1KB），cache line 对齐
    // ----------------

    size_t start = task->start;
    size_t end   = task->end;

    for (size_t i = start; i < end; i += BLOCK) {
        size_t limit = i + BLOCK;
        if (limit > end)
            limit = end;

        // 主循环：边预取边写
        for (size_t j = i; j < limit; j++) {
            // 提前预取下一个读位置
            if (j + PREFETCH_DIST < end)
                __builtin_prefetch(&src[j + PREFETCH_DIST], 0, 1);

            // 提前预取下一个写位置（写预取 hint = 1）
            if (j + PREFETCH_DIST < end)
                __builtin_prefetch(&dst[map[j + PREFETCH_DIST]], 1, 1);

            // 实际赋值
            dst[map[j]] = src[j];
        }
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



typedef struct {
    int8_t const         *src;
    int8_t               *dst;
    const reorder_entry_t *entries;
    size_t                start;
    size_t                end;
} reorder_entry_task_t;

static reorder_entry_task_t entry_tasks[REORDER_USE_CPUS];

// worker：按排序后 entries 写（dst 连续，src 可能随机）
void reorder_worker_entries(int cpu_id, void *arg)
{
    reorder_entry_task_t *task = (reorder_entry_task_t *)arg;
    const reorder_entry_t *entries = task->entries;
    const int8_t *src = task->src;
    int8_t *dst = task->dst;

    const size_t PREFETCH_DIST = 64;
    const size_t BLOCK = 256;

    size_t start = task->start;
    size_t end = task->end;

    for (size_t i = start; i < end; i += BLOCK) {
        size_t limit = i + BLOCK;
        if (limit > end) limit = end;

        for (size_t j = i; j < limit; j++) {
            // 预取将要访问的源（随机读）
            if (j + PREFETCH_DIST < end) {
                __builtin_prefetch(&src[entries[j + PREFETCH_DIST].src_index], 0, 1);
            }
            // 预取将要写入的目标（目标是连续的，prefetch optional）
            // __builtin_prefetch(&dst[entries[j + PREFETCH_DIST].dst_index], 1, 1);

            // 使用 entries 中的索引：注意 src 用 src_index，dst 用 dst_index
            dst[entries[j].dst_index] = src[entries[j].src_index];
        }
    }
}

// 调度函数：接收 entries（已按 dst_index 排序），按 entries 数量 chunk
void reorder_matrix_multi_core_entries(int8_t *dst,
                                       const int8_t *src,
                                       const reorder_entry_t *entries,
                                       size_t total_entries,
                                       int num_cores)
{
    if (num_cores > REORDER_USE_CPUS) num_cores = REORDER_USE_CPUS;
    size_t chunk = (total_entries + num_cores - 1) / num_cores;

    // 启动 secondary 核
    for (int c = 1; c < num_cores; c++) {
        reorder_entry_task_t *t = &entry_tasks[c];
        t->dst = dst;
        t->src = src;
        t->entries = entries;
        t->start = c * chunk;
        t->end = (c + 1) * chunk;
        if (t->end > total_entries) t->end = total_entries;

        launch_on_core(c, reorder_worker_entries, t);
    }

    // 主核处理 chunk0
    reorder_entry_task_t t0;
    t0.dst = dst;
    t0.src = src;
    t0.entries = entries;
    t0.start = 0;
    t0.end = (chunk > total_entries ? total_entries : chunk);
    reorder_worker_entries(0, &t0);

    wake_all_cores(num_cores);
    wait_all_cores(num_cores);
}
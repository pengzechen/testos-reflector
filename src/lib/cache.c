#include "t_types.h"
#include "t_sysreg.h"
#include "cache.h"
#include "lib/t_logger.h"

size_t      g_cache_line_size;

static inline void
__clean_dcache_one(const void *addr)
{
    __asm__ __volatile__("dc cvac, %0" : : "r"(addr));
}

static inline void
__invalidate_dcache_one(const void *addr)
{
    __asm__ __volatile__("dc ivac, %0" : : "r"(addr));
}

static inline void
__clean_and_invalidate_dcache_one(const void *addr)
{
    __asm__ __volatile__("dc civac, %0" ::"r"(addr));
}

int32_t
clean_dcache_va_range(const void *p, unsigned long size)
{
    const void *end;

    DSB_SY(); /* 确保之前的所有内存访问完成 */

    for (end = p + size; p < end; p += g_cache_line_size)
        __clean_dcache_one(p);

    DSB_SY(); /* 确保 clean 完成 */
    return 0;
}

int32_t
invalidate_dcache_va_range(const void *p, unsigned long size)
{
    size_t      off;
    const void *end             = p + size;

    DSB_SY(); /* So the CPU issues all writes to the range */

    off = (unsigned long) p % g_cache_line_size;
    if (off) {
        p -= off;
        __clean_and_invalidate_dcache_one(p);
        p += g_cache_line_size;
        size -= g_cache_line_size - off;
    }
    off = (unsigned long) end % g_cache_line_size;
    if (off) {
        end -= off;
        size -= off;
        __clean_and_invalidate_dcache_one(end);
    }

    for (; p < end; p += g_cache_line_size)
        __invalidate_dcache_one(p);

    DSB_SY(); /* So we know the flushes happen before continuing */

    return 0;
}

int32_t
clean_and_invalidate_dcache_va_range(const void *p, unsigned long size)
{
    const void *end;

    DSB_SY(); /* So the CPU issues all writes to the range */
    for (end = p + size; p < end; p += g_cache_line_size)
        __clean_and_invalidate_dcache_one(p);
    DSB_SY(); /* So we know the flushes happen before continuing */
    /* ARM callers assume that dcache_* functions cannot fail. */
    return 0;
}

void
init_cpu_cacheline_size(void)
{
    uint64_t ctr_el0;
    uint32_t dminline;

    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(ctr_el0));

    // 提取DminLine字段 (bits [19:16])
    dminline = (ctr_el0 >> CTR_EL0_DMINLINE_SHIFT) & CTR_EL0_DMINLINE_MASK;

    // 计算缓存行大小: WORD_SIZE * 2^DminLine
    size_t cache_size = CACHE_LINE_WORD_SIZE << dminline;

    // 验证缓存行大小的合理性
    if (cache_size < MIN_CACHELINE_SIZE || cache_size > MAX_CACHELINE_SIZE) {
        logger_error("cache init failed\n");
        return;  // 无效的缓存行大小
    }

    g_cache_line_size = cache_size;
}
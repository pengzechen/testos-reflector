
#ifndef _CACHE_H
#define _CACHE_H


#include "t_types.h"

/* CTR_EL0寄存器字段定义 */
#define CTR_EL0_DMINLINE_SHIFT 16   // DminLine字段位移
#define CTR_EL0_DMINLINE_MASK  0xF  // DminLine字段掩码
#define CACHE_LINE_WORD_SIZE   4    // 缓存行字大小

/* 缓存配置常量 */
#define DEFAULT_CACHELINE_SIZE 64   // 默认缓存行大小(字节)
#define MIN_CACHELINE_SIZE     16   // 最小缓存行大小(字节)
#define MAX_CACHELINE_SIZE     256  // 最大缓存行大小(字节)

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

static inline void ic_ivau(void *addr) {
    __asm__ volatile("ic ivau, %0" :: "r"(addr) : "memory");
}

static inline void
__clean_and_invalidate_dcache_one(const void *addr)
{
    __asm__ __volatile__("dc civac, %0" ::"r"(addr));
}

void
init_cpu_cacheline_size(void);

int32_t
clean_dcache_va_range(const void *p, unsigned long size);

int32_t
invalidate_dcache_va_range(const void *p, unsigned long size);


#endif // _CACHE_H
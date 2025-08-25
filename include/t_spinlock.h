#ifndef __T_SPINLOCK_H__
#define __T_SPINLOCK_H__

#include "t_types.h"

// 自旋锁结构
typedef struct
{
    volatile uint32_t lock;
} spinlock_t;

// 初始化自旋锁
static inline void
spinlock_init(spinlock_t *lock)
{
    lock->lock = 0;
}

// 获取自旋锁
static inline void
spin_lock(spinlock_t *lock)
{
    uint32_t tmp;

    asm volatile("1: ldaxr   %w0, %1\n"
                 "   cbnz    %w0, 1b\n"
                 "   stxr    %w0, %w2, %1\n"
                 "   cbnz    %w0, 1b\n"
                 : "=&r"(tmp), "+Q"(lock->lock)
                 : "r"(1)
                 : "memory");
}

// 尝试获取自旋锁（非阻塞）
static inline bool
spin_trylock(spinlock_t *lock)
{
    uint32_t tmp;

    asm volatile("   ldaxr   %w0, %1\n"
                 "   cbnz    %w0, 1f\n"
                 "   stxr    %w0, %w2, %1\n"
                 "1:\n"
                 : "=&r"(tmp), "+Q"(lock->lock)
                 : "r"(1)
                 : "memory");

    return tmp == 0;
}

// 释放自旋锁
static inline void
spin_unlock(spinlock_t *lock)
{
    asm volatile("stlr   %w1, %0\n" : "=Q"(lock->lock) : "r"(0) : "memory");
}

// 检查锁是否被持有
static inline bool
spin_is_locked(spinlock_t *lock)
{
    return lock->lock != 0;
}

#endif

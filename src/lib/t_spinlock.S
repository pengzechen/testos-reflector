/*
 * 单核安全的 Spinlock 模块
 * 使用普通 load/store 模拟原子操作
 * 在单核 CPU 或 MMU 未启用的情况下不会触发 IMPLEMENTATION DEFINED fault
 *
 * 对多核或 SMP，需要在 MMU + Cache 配置好后再用 LDAXR/STLXR
 */

.global spin_lock
.global spin_unlock
.global spin_trylock
.global spin_lock_irqsave
.global spin_unlock_irqrestore
.global spin_trylock_irqsave

/* 普通 spin_lock (忙等待) */
spin_lock:
1:  ldr w2, [x0]      // 加载锁状态
    cmp w2, #0
    b.ne 1b            // 如果锁被持有，继续自旋
    mov w1, #1
    str w1, [x0]       // 占用锁
    ret

/* 普通 spin_trylock */
spin_trylock:
    ldr w2, [x0]
    cmp w2, #0
    b.ne 2f
    mov w1, #1
    str w1, [x0]
    mov w0, #0         // 成功
    ret
2:  mov w0, #1         // 失败
    ret

/* 普通 spin_unlock */
spin_unlock:
    mov w1, #0
    str w1, [x0]
    ret

/* ---------------- IRQ 保存版本 ---------------- */

/*
 * spin_lock_irqsave - 获取锁并保存中断状态
 * x0 = spinlock_irq_t *lock
 */
spin_lock_irqsave:
    mrs x2, daif          // 保存 DAIF
    str w2, [x0, #4]      // 保存 irq_flags
    msr daifset, #2       // 禁用 IRQ
1:  ldr w2, [x0]          // 加载锁状态
    cmp w2, #0
    b.ne 1b
    mov w1, #1
    str w1, [x0]          // 占用锁
    ret

/* spin_trylock_irqsave - 尝试获取锁并保存中断状态 */
spin_trylock_irqsave:
    mrs x2, daif
    str w2, [x0, #4]
    msr daifset, #2
    ldr w2, [x0]
    cmp w2, #0
    b.ne 2f
    mov w1, #1
    str w1, [x0]
    mov w0, #0           // 成功
    ret
2:  ldr w2, [x0, #4]     // 获取锁失败，恢复 IRQ
    msr daif, x2
    mov w0, #1           // 失败
    ret

/* spin_unlock_irqrestore - 释放锁并恢复中断状态 */
spin_unlock_irqrestore:
    mov w1, #0
    str w1, [x0]         // 释放锁
    ldr w1, [x0, #4]     // 读取 irq_flags
    msr daif, x1         // 恢复中断
    ret

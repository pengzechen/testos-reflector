# Spinlock 使用指南

本文档介绍了TestOS中两种spinlock的使用方法：基本spinlock和带中断保护的spinlock。

## 基本Spinlock

基本spinlock适用于不需要中断保护的场景，例如用户态程序或者已知不会被中断打断的内核代码。

### 数据结构

```c
typedef struct {
    volatile int lock;
} spinlock_t;
```

### 使用方法

```c
#include "lib/t_spinlock.h"

// 声明和初始化
spinlock_t my_lock = SPINLOCK_INIT;

// 或者动态初始化
spinlock_t my_lock;
spinlock_init(&my_lock);

// 获取锁
spin_lock(&my_lock);
// 临界区代码
// ...
// 释放锁
spin_unlock(&my_lock);

// 尝试获取锁（非阻塞）
if (spin_trylock(&my_lock) == 0) {
    // 成功获取锁
    // 临界区代码
    // ...
    spin_unlock(&my_lock);
} else {
    // 获取锁失败，锁已被其他线程持有
}
```

## 带中断保护的Spinlock

带中断保护的spinlock在获取锁时会禁用IRQ中断，释放锁时恢复中断状态。这样可以防止在持有锁期间被中断打断，避免死锁。

### 数据结构

```c
typedef struct {
    volatile int lock;        // 锁状态，0=未锁定，1=已锁定
    volatile uint32_t irq_flags;  // 保存的中断状态
} spinlock_irq_t;
```

### 使用方法

```c
#include "lib/t_spinlock.h"

// 声明和初始化
spinlock_irq_t my_irq_lock = SPINLOCK_IRQ_INIT;

// 或者动态初始化
spinlock_irq_t my_irq_lock;
spinlock_irq_init(&my_irq_lock);

// 获取锁并禁用中断
spin_lock_irqsave(&my_irq_lock);
// 临界区代码（此时IRQ中断被禁用）
// ...
// 释放锁并恢复中断状态
spin_unlock_irqrestore(&my_irq_lock);

// 尝试获取锁（非阻塞）
if (spin_trylock_irqsave(&my_irq_lock) == 0) {
    // 成功获取锁，IRQ中断已被禁用
    // 临界区代码
    // ...
    spin_unlock_irqrestore(&my_irq_lock);
} else {
    // 获取锁失败，中断状态已恢复
}
```

## 何时使用哪种Spinlock

### 使用基本Spinlock的场景：
- 用户态程序
- 已知不会被中断打断的内核代码
- 性能要求极高且确保不会有中断问题的场景

### 使用带中断保护的Spinlock的场景：
- 内核中断处理程序
- 可能被中断打断的内核代码
- 需要保证原子性的关键代码段
- 多核系统中的共享资源保护

## 注意事项

1. **避免长时间持有锁**：Spinlock是忙等待锁，长时间持有会浪费CPU资源。

2. **避免嵌套锁**：不要在持有一个锁的情况下尝试获取另一个锁，可能导致死锁。

3. **中断保护的开销**：带中断保护的spinlock有额外的中断禁用/恢复开销，只在必要时使用。

4. **锁的粒度**：尽量使用细粒度的锁，减少锁竞争。

5. **内存屏障**：我们的实现已经包含了必要的内存屏障，确保内存操作的顺序性。

## 测试

可以使用提供的测试函数来验证spinlock的功能：

```c
#include "lib/t_spinlock_test.h"

// 运行所有测试
run_spinlock_tests();

// 或者单独运行测试
test_basic_spinlock();
test_irq_spinlock();
```

## 实现细节

- 使用ARMv8-A的原子指令（LDAXR/STLXR）实现
- 包含适当的内存屏障（DMB ISH）
- 中断控制通过DAIF寄存器实现
- 支持多核系统的缓存一致性

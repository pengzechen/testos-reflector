/*
 * Spinlock测试头文件
 */

#ifndef T_SPINLOCK_TEST_H
#define T_SPINLOCK_TEST_H

/*
 * 运行所有spinlock测试
 * 包括基本spinlock和带中断保护的spinlock测试
 */
void
run_spinlock_tests(void);

/*
 * 测试基本spinlock功能
 */
void
test_basic_spinlock(void);

/*
 * 测试带中断保护的spinlock功能
 */
void
test_irq_spinlock(void);

#endif  // T_SPINLOCK_TEST_H

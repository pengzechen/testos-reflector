
#ifndef T_MEM_H
#define T_MEM_H

#include "t_types.h"

/**
 * 内存管理模块
 * 
 * 提供基于空闲链表的内存分配器，支持内存分配和回收
 * 使用 best-fit 策略查找合适的内存块
 */

/**
 * 初始化内存管理器
 * @param heap_size 堆大小（字节）
 */
void t_mem_init(size_t heap_size);

/**
 * 分配内存
 * @param size 请求的内存大小（字节）
 * @return 分配的内存地址，失败返回 NULL
 */
void *t_mem_alloc(size_t size);

/**
 * 释放内存
 * @param ptr 要释放的内存地址
 */
void t_mem_free(void *ptr);

/**
 * 获取堆使用统计信息
 * @param total_size 输出参数：总堆大小
 * @param used_size 输出参数：已使用大小
 * @param free_size 输出参数：空闲大小
 * @param free_blocks 输出参数：空闲块数量
 */
void t_mem_stats(size_t *total_size, size_t *used_size, 
                 size_t *free_size, size_t *free_blocks);

/**
 * 打印内存管理器状态（调试用）
 */
void t_mem_dump(void);

/**
 * 基础测试套件
 */
void t_mem_run_tests(void);

/**
 * 压力测试套件（包括边界测试、错误检测、性能测试等）
 */
void t_mem_run_stress_tests(void);

#endif /* T_MEM_H */

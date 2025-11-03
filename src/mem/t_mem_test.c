
#include "mem/t_mem.h"
#include "lib/t_logger.h"
#include "lib/rand.h"
#include "lib/t_string.h"
#include "dev/t_timer.h"

// 简单的伪随机数生成器（如果系统没有）
static uint32_t test_rand_seed = 12345;
static uint32_t test_rand(void) {
    test_rand_seed = test_rand_seed * 1103515245 + 12345;
    return (test_rand_seed / 65536) % 32768;
}

/**
 * 内存管理测试示例
 * 演示内存分配和回收功能
 */
void t_mem_test_basic(void)
{
    logger_info("\n=== Basic Memory Allocation Test ===\n");
    
    // 分配一些内存块
    void *ptr1 = t_mem_alloc(1024);
    void *ptr2 = t_mem_alloc(2048);
    void *ptr3 = t_mem_alloc(512);
    
    logger_info("Allocated ptr1: %p (1024 bytes)\n", ptr1);
    logger_info("Allocated ptr2: %p (2048 bytes)\n", ptr2);
    logger_info("Allocated ptr3: %p (512 bytes)\n", ptr3);
    
    // 打印内存状态
    t_mem_dump();
    
    // 释放中间的块
    logger_info("\nFreeing ptr2...\n");
    t_mem_free(ptr2);
    
    // 打印内存状态
    t_mem_dump();
    
    // 再次分配（应该使用刚释放的空间）
    void *ptr4 = t_mem_alloc(1024);
    logger_info("\nAllocated ptr4: %p (1024 bytes)\n", ptr4);
    
    // 打印内存状态
    t_mem_dump();
    
    // 释放所有
    logger_info("\nFreeing all...\n");
    t_mem_free(ptr1);
    t_mem_free(ptr3);
    t_mem_free(ptr4);
    
    // 打印最终状态
    t_mem_dump();
}

void t_mem_test_fragmentation(void)
{
    logger_info("\n=== Memory Fragmentation Test ===\n");
    
    // 分配多个小块
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = t_mem_alloc(256);
        logger_info("Allocated ptr[%d]: %p\n", i, ptrs[i]);
    }
    
    t_mem_dump();
    
    // 释放一些块（形成碎片）
    logger_info("\nFreeing alternate blocks...\n");
    for (int i = 0; i < 10; i += 2) {
        t_mem_free(ptrs[i]);
    }
    
    t_mem_dump();
    
    // 尝试分配一个大块（测试合并）
    logger_info("\nAllocating large block...\n");
    void *large = t_mem_alloc(4096);
    logger_info("Large block: %p\n", large);
    
    t_mem_dump();
    
    // 清理
    for (int i = 1; i < 10; i += 2) {
        t_mem_free(ptrs[i]);
    }
    if (large) {
        t_mem_free(large);
    }
    
    t_mem_dump();
}

void t_mem_test_coalesce(void)
{
    logger_info("\n=== Memory Coalescing Test ===\n");
    
    // 分配连续的块
    void *ptr1 = t_mem_alloc(1024);
    void *ptr2 = t_mem_alloc(1024);
    void *ptr3 = t_mem_alloc(1024);
    
    logger_info("Allocated 3 consecutive blocks:\n");
    logger_info("  ptr1: %p\n", ptr1);
    logger_info("  ptr2: %p\n", ptr2);
    logger_info("  ptr3: %p\n", ptr3);
    
    t_mem_dump();
    
    // 释放中间的块
    logger_info("\nFreeing ptr2...\n");
    t_mem_free(ptr2);
    t_mem_dump();
    
    // 释放第一个块（应该与ptr2合并）
    logger_info("\nFreeing ptr1 (should coalesce with ptr2)...\n");
    t_mem_free(ptr1);
    t_mem_dump();
    
    // 释放第三个块（应该合并成一个大块）
    logger_info("\nFreeing ptr3 (should coalesce all)...\n");
    t_mem_free(ptr3);
    t_mem_dump();
}

/**
 * 边界测试：测试极端大小的分配
 */
void t_mem_test_boundary(void)
{
    logger_info("\n=== Boundary Test ===\n");
    
    // 测试最小分配
    logger_info("Testing minimal allocation (1 byte)...\n");
    void *tiny = t_mem_alloc(1);
    if (tiny) {
        logger_info("  Allocated 1 byte at %p\n", tiny);
        t_mem_free(tiny);
    }
    
    // 测试零大小
    logger_info("Testing zero size allocation...\n");
    void *zero = t_mem_alloc(0);
    logger_info("  Result: %p (should be NULL)\n", zero);
    
    // 测试非常大的分配（应该失败 - 超过堆大小）
    size_t heap_total, heap_used, heap_free, heap_blocks;
    t_mem_stats(&heap_total, &heap_used, &heap_free, &heap_blocks);
    
    logger_info("Testing huge allocation (%lu MB, larger than heap)...\n", 
               (heap_total + 1024*1024) / (1024*1024));
    void *huge = t_mem_alloc(heap_total + 1024*1024);  // 比堆大 1MB
    if (huge == NULL) {
        logger_info("  Correctly failed to allocate\n");
    } else {
        logger_info("  ERROR: Unexpectedly succeeded at %p\n", huge);
        t_mem_free(huge);
    }
    
    // 测试接近堆大小的分配
    t_mem_stats(&heap_total, &heap_used, &heap_free, &heap_blocks);
    logger_info("Testing near-heap-size allocation (%lu KB)...\n", (heap_free - 1024) / 1024);
    void *almost_all = t_mem_alloc(heap_free - 1024);
    if (almost_all) {
        logger_info("  Allocated successfully at %p\n", almost_all);
        t_mem_free(almost_all);
    } else {
        logger_info("  Failed to allocate (might be due to headers overhead)\n");
    }
    
    t_mem_dump();
}

/**
 * 错误检测测试
 */
void t_mem_test_errors(void)
{
    logger_info("\n=== Error Detection Test ===\n");
    
    // 测试双重释放
    logger_info("Testing double free detection...\n");
    void *ptr = t_mem_alloc(512);
    logger_info("  Allocated at %p\n", ptr);
    logger_info("  First free...\n");
    t_mem_free(ptr);
    logger_info("  Second free (should warn)...\n");
    t_mem_free(ptr);
    
    // 测试空指针释放
    logger_info("\nTesting NULL pointer free...\n");
    t_mem_free(NULL);
    logger_info("  NULL free handled\n");
    
    // 测试无效指针释放（堆外地址）
    logger_info("\nTesting invalid pointer free...\n");
    void *invalid = (void *)0x1000;
    t_mem_free(invalid);
    logger_info("  Invalid pointer handled\n");
    
    t_mem_dump();
}

/**
 * 压力测试：大量随机分配和释放
 */
void t_mem_test_stress(void)
{
    logger_info("\n=== Stress Test ===\n");
    
    #define MAX_PTRS 100
    void *ptrs[MAX_PTRS];
    int allocated[MAX_PTRS];
    int alloc_count = 0;
    int free_count = 0;
    int failed_alloc = 0;
    
    // 初始化
    for (int i = 0; i < MAX_PTRS; i++) {
        ptrs[i] = NULL;
        allocated[i] = 0;
    }
    
    logger_info("Running 500 random operations...\n");
    
    // 执行 500 次随机操作
    for (int op = 0; op < 500; op++) {
        int action = test_rand() % 100;
        
        if (action < 60) {  // 60% 概率分配
            // 寻找空闲槽位
            int slot = -1;
            for (int i = 0; i < MAX_PTRS; i++) {
                if (allocated[i] == 0) {
                    slot = i;
                    break;
                }
            }
            
            if (slot != -1) {
                // 随机大小：64B 到 16KB
                size_t size = 64 + (test_rand() % (16 * 1024));
                ptrs[slot] = t_mem_alloc(size);
                
                if (ptrs[slot]) {
                    allocated[slot] = size;
                    alloc_count++;
                    
                    // 写入一些数据验证内存可用
                    if (size >= 8) {
                        uint64_t *p = (uint64_t *)ptrs[slot];
                        *p = 0xDEADBEEFCAFEBABE;
                    }
                } else {
                    failed_alloc++;
                }
            }
        } else {  // 40% 概率释放
            // 寻找已分配的块
            int slot = -1;
            int tries = 0;
            while (tries < 10) {
                int idx = test_rand() % MAX_PTRS;
                if (allocated[idx] != 0) {
                    slot = idx;
                    break;
                }
                tries++;
            }
            
            if (slot != -1) {
                // 验证数据
                if (allocated[slot] >= 8) {
                    uint64_t *p = (uint64_t *)ptrs[slot];
                    if (*p != 0xDEADBEEFCAFEBABE) {
                        logger_error("  [ERROR] Memory corruption detected at slot %d!\n", slot);
                    }
                }
                
                t_mem_free(ptrs[slot]);
                ptrs[slot] = NULL;
                allocated[slot] = 0;
                free_count++;
            }
        }
        
        // 每 100 次操作打印统计
        if ((op + 1) % 100 == 0) {
            size_t total, used, free, blocks;
            t_mem_stats(&total, &used, &free, &blocks);
            logger_info("  Op %d: Alloc=%d, Free=%d, Failed=%d, Used=%lu KB, Free blocks=%lu\n",
                       op + 1, alloc_count, free_count, failed_alloc, used / 1024, blocks);
        }
    }
    
    // 清理所有剩余的分配
    logger_info("\nCleaning up remaining allocations...\n");
    int remaining = 0;
    size_t remaining_size = 0;
    for (int i = 0; i < MAX_PTRS; i++) {
        if (allocated[i] != 0) {
            t_mem_free(ptrs[i]);
            remaining++;
            remaining_size += allocated[i];
        }
    }
    
    logger_info("Stress test completed:\n");
    logger_info("  Total allocations: %d\n", alloc_count);
    logger_info("  Total frees: %d\n", free_count);
    logger_info("  Failed allocations: %d\n", failed_alloc);
    logger_info("  Remaining cleaned: %d (%lu KB)\n", remaining, remaining_size / 1024);
    
    t_mem_dump();
}

/**
 * 性能测试
 */
void t_mem_test_performance(void)
{
    logger_info("\n=== Performance Test ===\n");
    
    #define PERF_ITERATIONS 1000
    uint32_t start_tick, end_tick;
    
    // 测试小块分配性能
    logger_info("Testing small block allocation (1000x 256B)...\n");
    start_tick = timer_get_system_ticks();
    
    void *small_ptrs[PERF_ITERATIONS];
    for (int i = 0; i < PERF_ITERATIONS; i++) {
        small_ptrs[i] = t_mem_alloc(256);
    }
    
    end_tick = timer_get_system_ticks();
    logger_info("  Allocation time: %u ticks\n", end_tick - start_tick);
    
    // 测试释放性能
    start_tick = timer_get_system_ticks();
    for (int i = 0; i < PERF_ITERATIONS; i++) {
        t_mem_free(small_ptrs[i]);
    }
    end_tick = timer_get_system_ticks();
    logger_info("  Free time: %u ticks\n", end_tick - start_tick);
    
    // 测试大块分配性能
    logger_info("\nTesting large block allocation (100x 64KB)...\n");
    start_tick = timer_get_system_ticks();
    
    void *large_ptrs[100];
    for (int i = 0; i < 100; i++) {
        large_ptrs[i] = t_mem_alloc(64 * 1024);
    }
    
    end_tick = timer_get_system_ticks();
    logger_info("  Allocation time: %u ticks\n", end_tick - start_tick);
    
    // 清理
    for (int i = 0; i < 100; i++) {
        if (large_ptrs[i]) {
            t_mem_free(large_ptrs[i]);
        }
    }
    
    t_mem_dump();
}

/**
 * 内存写入验证测试
 */
void t_mem_test_write_verify(void)
{
    logger_info("\n=== Write Verification Test ===\n");
    
    #define VERIFY_BLOCKS 20
    void *ptrs[VERIFY_BLOCKS];
    size_t sizes[VERIFY_BLOCKS] = {
        64, 128, 256, 512, 1024, 2048, 4096, 8192,
        100, 200, 300, 400, 500, 600, 700, 800,
        1500, 3000, 5000, 10000
    };
    
    logger_info("Allocating and writing to %d blocks...\n", VERIFY_BLOCKS);
    
    // 分配并写入模式
    for (int i = 0; i < VERIFY_BLOCKS; i++) {
        ptrs[i] = t_mem_alloc(sizes[i]);
        if (ptrs[i]) {
            // 写入特定模式
            uint8_t *p = (uint8_t *)ptrs[i];
            for (size_t j = 0; j < sizes[i]; j++) {
                p[j] = (uint8_t)(i + j);
            }
            logger_info("  Block %d: %lu bytes at %p\n", i, sizes[i], ptrs[i]);
        } else {
            logger_error("  Block %d: allocation failed!\n", i);
        }
    }
    
    // 验证数据
    logger_info("\nVerifying data integrity...\n");
    int errors = 0;
    for (int i = 0; i < VERIFY_BLOCKS; i++) {
        if (ptrs[i]) {
            uint8_t *p = (uint8_t *)ptrs[i];
            for (size_t j = 0; j < sizes[i]; j++) {
                if (p[j] != (uint8_t)(i + j)) {
                    logger_error("  Block %d offset %lu: expected 0x%02x, got 0x%02x\n",
                               i, j, (uint8_t)(i + j), p[j]);
                    errors++;
                    if (errors >= 5) break;  // 只报告前5个错误
                }
            }
        }
    }
    
    if (errors == 0) {
        logger_info("  All data verified successfully!\n");
    } else {
        logger_error("  Found %d errors!\n", errors);
    }
    
    // 清理
    for (int i = 0; i < VERIFY_BLOCKS; i++) {
        if (ptrs[i]) {
            t_mem_free(ptrs[i]);
        }
    }
    
    t_mem_dump();
}

/**
 * 运行所有内存测试
 */
void t_mem_run_tests(void)
{
    logger_info("\n========================================\n");
    logger_info("   Memory Manager Test Suite\n");
    logger_info("========================================\n");
    
    t_mem_test_basic();
    t_mem_test_fragmentation();
    t_mem_test_coalesce();
    
    logger_info("\n========================================\n");
    logger_info("   All tests completed\n");
    logger_info("========================================\n");
}

/**
 * 运行完整的压力测试套件
 */
void t_mem_run_stress_tests(void)
{
    logger_info("\n========================================\n");
    logger_info("   Memory Manager Stress Test Suite\n");
    logger_info("========================================\n");
    
    t_mem_test_boundary();
    t_mem_test_errors();
    t_mem_test_write_verify();
    t_mem_test_stress();
    t_mem_test_performance();
    
    logger_info("\n========================================\n");
    logger_info("   All stress tests completed\n");
    logger_info("========================================\n");
}

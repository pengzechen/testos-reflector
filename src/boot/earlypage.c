
#include "t_types.h"
#include "mmu.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"

#define PAGE_TABLE_ENTRIES   512   // 每个页表的条目数
#define PAGE_TABLE_ALIGNMENT 4096  // 页表对齐要求 (4KB)

/* 页表定义 - 启动时使用的简单映射 */
static uint64_t pt0[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_TABLE_ALIGNMENT), section(".data")));  // L0 页表

static uint64_t pt1[PAGE_TABLE_ENTRIES]
    __attribute__((aligned(PAGE_TABLE_ALIGNMENT), section(".data")));  // L1 页表
/**
 * @brief 设置页表项指向下一级页表
 * @param pte 页表项指针
 * @param next_table 下一级页表地址
 */
static inline void
set_table_entry(uint64_t *pte, uint64_t *next_table)
{
    *pte = (uint64_t) next_table | MM_TYPE_TABLE;
}

/**
 * @brief 设置1GB块映射页表项
 * @param pte 页表项指针
 * @param base_addr 物理基址
 * @param memory_type 内存类型标志
 * @param access_flags 访问权限标志
 */
static inline void
set_block_entry(uint64_t *pte, uint64_t base_addr, uint64_t memory_type, uint64_t access_flags)
{
    *pte = base_addr | memory_type | access_flags;
}

void
init_page_table()
{
    memset((void *) pt0, 0, sizeof(pt0));
    memset((void *) pt1, 0, sizeof(pt1));

    // 设置低地址空间页表 (TTBR0_EL1)
    set_table_entry(&pt0[0], pt1);  // L0[0] -> L1页表

    // L1页表映射 - 每项映射1GB
    set_block_entry(&pt1[0], 0x00000000, PTE_NORMAL_MEMORY, 0);  // 普通内存

    set_block_entry(&pt1[1], 0x40000000, PTE_NORMAL_MEMORY, 0);  // 普通内存

    set_block_entry(&pt1[2], 0x80000000, PTE_NORMAL_MEMORY, 0);  // 普通内存

    set_block_entry(&pt1[3], 0xc0000000, PTE_DEVICE_MEMORY, 0);  // 设备内存
}

void
enable_mmu(void)
{
    extern void init_mmu(uint64_t);
    init_mmu((uint64_t) (void *) pt0);
}
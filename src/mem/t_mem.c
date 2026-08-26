
#include "mem/t_mem.h"
#include "lib/t_string.h"

// 使用系统的 logger 函数
extern int logger_info(const char *fmt, ...);
extern int logger_error(const char *fmt, ...);
extern int logger_warn(const char *fmt, ...);

/**
 * 内存块头结构
 * 每个内存块（无论空闲还是已分配）都有这个头部
 */
// 内存对齐大小（64字节对齐，适配 NPU DMA）
#define MEM_ALIGNMENT 64
#define MEM_ALIGN(size) (((size) + (MEM_ALIGNMENT - 1)) & ~(MEM_ALIGNMENT - 1))

typedef struct mem_block {
    size_t size;              // 块大小（不包括头部）
    int is_free;              // 1: 空闲, 0: 已分配
    struct mem_block *next;   // 指向下一个块（空闲链表或物理地址顺序）
    struct mem_block *prev;   // 指向前一个块

    /*
     * 填充：让头部大小恰好等于 MEM_ALIGNMENT(64) 字节。
     *
     * 返回给调用者的数据指针 = (uint8_t*)block + sizeof(mem_block_t)。
     * 只有当头部大小本身是 64 的倍数、且 block 起始 64 对齐时，
     * 该数据指针才是 64 对齐。
     *
     * 之前头部只有 32 字节（size_t+int+两个指针），于是每次分配返回的
     * 指针只有 32 字节对齐。是否落到 64 边界取决于此前所有分配的累计
     * 大小 —— 这会随分配顺序漂移。NPU 的 DMA 缓冲区（regcmd/输入/权重/
     * 输出）一旦落到非 64 对齐地址，大尺寸 matmul 的描述符按 64 步幅
     * 寻址会越界，硬件报总线错误 int_raw=0xc0000000（不在 int_mask=0x300
     * 内，无完成中断 → Job wait timeout）。这正是“加了 grouped conv 测试
     * （泄漏 21664B, 21664%64==32，翻转了后续所有分配的 64/32 对齐奇偶）
     * 之后 YOLO 首个 conv 偶发卡死”的根因，而非某个硬件位没有恢复。
     *
     * 头部本身对齐到 MEM_ALIGNMENT 后，配合 split_block 里 64 对齐的块大小，
     * 保证每一个返回指针都稳定 64 对齐，消除该奇偶翻转。
     */
    uint8_t _pad[MEM_ALIGNMENT
                 - sizeof(struct { size_t a; int b; void *c; void *d; })];
} mem_block_t;

/* 编译期确保头部恰好一个对齐粒度大小；否则返回指针不再 64 对齐。 */
_Static_assert(sizeof(mem_block_t) == MEM_ALIGNMENT,
               "mem_block_t header must equal MEM_ALIGNMENT for aligned payload");

// 最小块大小（避免碎片过小）
#define MIN_BLOCK_SIZE 64

// 堆管理变量
static uint8_t *heap_start = NULL;
static uint8_t *heap_end = NULL;
static size_t heap_total_size = 0;
static mem_block_t *free_list_head = NULL;  // 空闲链表头

extern void __heap_flag(void);

/**
 * 初始化内存管理器
 */
void t_mem_init(size_t heap_size)
{
    // 获取链接脚本定义的堆起始地址，并对齐到 MEM_ALIGNMENT。
    // 链接脚本当前已 16KB 对齐（ALIGN(1<<14)），此处再兜底一次：
    // 保证即使链接布局改动，t_mem_alloc 返回的数据指针仍稳定 64 对齐
    // （头部大小 == 64，见 mem_block_t 定义处的说明）。
    uint64_t raw     = (uint64_t)&__heap_flag;
    uint64_t aligned = (raw + (MEM_ALIGNMENT - 1)) & ~(uint64_t)(MEM_ALIGNMENT - 1);
    heap_start = (uint8_t *)aligned;
    heap_total_size = heap_size - (size_t)(aligned - raw);
    heap_end = heap_start + heap_total_size;

    // 初始化第一个空闲块（整个堆）
    free_list_head = (mem_block_t *)heap_start;
    free_list_head->size = heap_total_size - sizeof(mem_block_t);
    free_list_head->is_free = 1;
    free_list_head->next = NULL;
    free_list_head->prev = NULL;

    logger_info("t_mem initialized: start=0x%lx, size=%lu KB\n",
               (unsigned long)heap_start, heap_total_size / 1024);
}

/**
 * 从空闲链表中移除一个块
 */
static void remove_from_free_list(mem_block_t *block)
{
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_list_head = block->next;
    }
    
    if (block->next) {
        block->next->prev = block->prev;
    }
    
    block->next = NULL;
    block->prev = NULL;
}

/**
 * 将块添加到空闲链表（按地址排序）
 */
static void add_to_free_list(mem_block_t *block)
{
    block->is_free = 1;

    // 如果空闲链表为空，直接插入
    if (free_list_head == NULL) {
        free_list_head = block;
        block->next = NULL;
        block->prev = NULL;
        return;
    }

    // 按地址顺序插入（方便合并相邻块）
    mem_block_t *current = free_list_head;
    mem_block_t *prev_block = NULL;

    while (current != NULL && current < block) {
        prev_block = current;
        current = current->next;
    }

    // 插入到 prev_block 和 current 之间
    block->next = current;
    block->prev = prev_block;

    if (prev_block) {
        prev_block->next = block;
    } else {
        free_list_head = block;
    }

    if (current) {
        current->prev = block;
    }
}

/**
 * 合并相邻的空闲块
 */
static void merge_free_blocks(mem_block_t *block)
{
    // 与下一个块合并（物理地址上的下一个块）
    uint8_t *next_addr = (uint8_t *)block + sizeof(mem_block_t) + block->size;
    if (next_addr < heap_end) {
        mem_block_t *next_block = (mem_block_t *)next_addr;
        // 只有当下一个块也在空闲链表中时才合并
        if (next_block->is_free) {
            block->size += sizeof(mem_block_t) + next_block->size;
            remove_from_free_list(next_block);
        }
    }

    // 与前一个块合并（检查链表中前一个节点是否物理相邻）
    if (block->prev && block->prev->is_free) {
        uint8_t *prev_end = (uint8_t *)block->prev + sizeof(mem_block_t) + block->prev->size;
        if (prev_end == (uint8_t *)block) {
            block->prev->size += sizeof(mem_block_t) + block->size;
            remove_from_free_list(block);
        }
    }
}

/**
 * 分割空闲块
 */
static void split_block(mem_block_t *block, size_t size)
{
    // 如果剩余空间足够大，分割出一个新的空闲块
    size_t remaining = block->size - size;
    if (remaining > sizeof(mem_block_t) + MIN_BLOCK_SIZE) {
        mem_block_t *new_block = (mem_block_t *)((uint8_t *)block + sizeof(mem_block_t) + size);
        new_block->size = remaining - sizeof(mem_block_t);
        new_block->is_free = 1;
        
        block->size = size;
        
        // 将新块插入空闲链表
        add_to_free_list(new_block);
    }
}

/**
 * 分配内存（best-fit 策略）
 */
void *t_mem_alloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    // 对齐大小
    size = MEM_ALIGN(size);

    // 使用 best-fit 策略查找最合适的空闲块
    mem_block_t *best_fit = NULL;
    mem_block_t *current = free_list_head;

    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            if (best_fit == NULL || current->size < best_fit->size) {
                best_fit = current;
            }
        }
        current = current->next;
    }

    if (best_fit == NULL) {
        logger_error("t_mem_alloc: out of memory (requested %lu bytes)\n", size);
        return NULL;
    }

    // 从空闲链表中移除
    remove_from_free_list(best_fit);

    // 如果块足够大，分割它
    split_block(best_fit, size);

    // 标记为已分配
    best_fit->is_free = 0;

    // 返回数据区域的地址（跳过头部）
    void *ptr = (void *)((uint8_t *)best_fit + sizeof(mem_block_t));
    
    return ptr;
}

/**
 * 释放内存
 */
void t_mem_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    // 获取块头地址
    mem_block_t *block = (mem_block_t *)((uint8_t *)ptr - sizeof(mem_block_t));

    // 检查地址有效性
    if ((uint8_t *)block < heap_start || (uint8_t *)block >= heap_end) {
        logger_error("t_mem_free: invalid pointer 0x%lx\n", (unsigned long)ptr);
        return;
    }

    if (block->is_free) {
        logger_warn("t_mem_free: double free detected at 0x%lx\n", (unsigned long)ptr);
        return;
    }

    // 将块添加回空闲链表
    add_to_free_list(block);

    // 尝试合并相邻的空闲块
    merge_free_blocks(block);
}

/**
 * 获取内存统计信息
 */
void t_mem_stats(size_t *total_size, size_t *used_size, 
                 size_t *free_size, size_t *free_blocks)
{
    if (total_size) {
        *total_size = heap_total_size;
    }

    size_t total_free = 0;
    size_t num_free_blocks = 0;

    mem_block_t *current = free_list_head;
    while (current != NULL) {
        if (current->is_free) {
            total_free += current->size + sizeof(mem_block_t);
            num_free_blocks++;
        }
        current = current->next;
    }

    if (used_size) {
        *used_size = heap_total_size - total_free;
    }
    
    if (free_size) {
        *free_size = total_free;
    }
    
    if (free_blocks) {
        *free_blocks = num_free_blocks;
    }
}

/**
 * 打印内存管理器状态
 */
void t_mem_dump(void)
{
    size_t total, used, free, blocks;
    t_mem_stats(&total, &used, &free, &blocks);

    logger_info("=== Memory Manager Status ===\n");
    logger_info("Total heap: %lu KB (%lu bytes)\n", total / 1024, total);
    
    // 修复百分比计算：先乘1000再除，保留一位小数
    size_t used_percent = (used * 1000) / total;  // 千分比
    size_t free_percent = (free * 1000) / total;
    
    logger_info("Used: %lu KB (%lu.%lu%%) [%lu bytes]\n", 
               used / 1024, used_percent / 10, used_percent % 10, used);
    logger_info("Free: %lu KB (%lu.%lu%%) [%lu bytes]\n", 
               free / 1024, free_percent / 10, free_percent % 10, free);
    logger_info("Free blocks: %lu\n", blocks);
    
    logger_info("Free list:\n");
    mem_block_t *current = free_list_head;
    int idx = 0;
    while (current != NULL && idx < 10) {
        if (current->is_free) {
            logger_info("  [%d] addr=0x%lx, size=%lu bytes\n", 
                       idx, (unsigned long)current, current->size);
            idx++;
        }
        current = current->next;
    }
}

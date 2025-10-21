

#include "rkmem.h"

static uint8_t *heap_start = NULL;
static uint8_t *heap_end = NULL;
static uint8_t *heap_ptr = NULL;

void rkmem_init(size_t heap_size)
{
    // 链接脚本定义的堆起始符号
    heap_start = (uint8_t *)&__heap_flag;
    heap_end   = heap_start + heap_size;
    heap_ptr   = heap_start;
}

void *rkmem_alloc(size_t size)
{
    // 简单 4095 字节对齐
    size = (size + 4095) & ~4095;

    if (heap_ptr + size > heap_end) {
        // 堆已满
        return NULL;
    }

    void *allocated = heap_ptr;
    heap_ptr += size;
    return allocated;
}

void rkmem_free(void *ptr)
{
    // 简单分配器不支持回收
    (void)ptr;
}
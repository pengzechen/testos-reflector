#ifndef DBG_H
#define DBG_H

#include "t_types.h"
#include "mem/cache.h"


#define BRK_INSTR 0xd4200000u  // BRK #0 指令

// 定义最大可存储的替换数量
#define MAX_PATCHES 10

// 存储替换信息的结构体
typedef struct {
    void *addr;  // 被替换的地址
    uint32_t orig_instr;  // 原始指令
} patch_entry_t;

int
patch(void *addr);

int
restore(void *addr);

#endif // DBG_H
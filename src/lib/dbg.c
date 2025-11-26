
#include "lib/dbg.h"
#include "t_sysreg.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"

static patch_entry_t patches[MAX_PATCHES];
static int           patch_count = 0;

// 缓存维护函数，确保修改生效
static inline void
cache_flush(void *addr)
{
    __clean_and_invalidate_dcache_one(addr);  // 将数据缓存写回到内存
    DSB_ISH();                                // 确保缓存写回完成
    ic_ivau(addr);                            // 使指令缓存失效
    ISB();                                    // 确保指令缓存刷新完成
}

// `patch` 函数：将目标地址的指令替换为 BRK
int
patch(void *addr)
{
    // 如果已经替换了最大数量的地址，返回错误
    if (patch_count >= MAX_PATCHES) {
        logger_warn("Reached maximum patch count\n");
        return -1;
    }
    // 保存原始指令
    uint32_t orig_instr = *(volatile uint32_t *) addr;

    // 插入 BRK #0 指令
    *(volatile uint32_t *) addr = BRK_INSTR;
    // 缓存维护
    cache_flush(addr);

    // 存储替换信息
    patches[patch_count].addr       = addr;
    patches[patch_count].orig_instr = orig_instr;
    patch_count++;
    return 0;  // 成功
}

// `restore` 函数：恢复原始指令
int
restore(void *addr)
{
    // 查找该地址对应的替换记录
    for (int i = 0; i < patch_count; i++) {
        if (patches[i].addr == addr) {
            // 恢复原始指令
            *(volatile uint32_t *) addr = patches[i].orig_instr;
            // 缓存维护
            cache_flush(addr);
            // 删除当前记录
            memmove(&patches[i], &patches[i + 1], sizeof(patch_entry_t) * (patch_count - i - 1));
            patch_count--;
            return 0;  // 成功
        }
    }
    logger_warn("Patch not found for address %p\n", addr);
    return -1;  // 未找到对应的替换记录
}

#ifndef __RKNPU_H__
#define __RKNPU_H__

/*
 * ==========================================
 *  RKNPU 寄存器偏移量定义
 * ==========================================
 */

#define RKNPU_VERSION           0x0000
#define RKNPU_VERSION_NUM       0x0004
#define RKNPU_PC_OP_EN          0x0008
#define RKNPU_PC_DATA_ADDR      0x0010
#define RKNPU_PC_DATA_AMOUNT    0x0014
#define RKNPU_INT_MASK          0x0020
#define RKNPU_INT_CLEAR         0x0024
#define RKNPU_INT_STATUS        0x0028
#define RKNPU_INT_RAW_STATUS    0x002C
#define RKNPU_PC_TASK_CONTROL   0x0030
#define RKNPU_PC_DMA_BASE_ADDR  0x0034
#define RKNPU_PC_TASK_STATUS    0x003C
#define RKNPU_ENABLE_MASK       0xF008
#define RKNPU_CLR_ALL_RW_AMOUNT 0x8010
#define RKNPU_DT_WR_AMOUNT      0x8034
#define RKNPU_DT_RD_AMOUNT      0x8038
#define RKNPU_WT_RD_AMOUNT      0x803C


typedef struct __attribute__((packed))
{
    uint32_t flags;
    uint32_t op_idx;
    uint32_t enable_mask;
    uint32_t int_mask;
    uint32_t int_clear;
    uint32_t int_status;
    uint32_t regcfg_amount;
    uint32_t regcfg_offset;
    uint64_t regcmd_addr;
} npu_task_t;


typedef struct
{
    uint32_t task_start;
    uint32_t task_number;
} npu_subcore_task_t;

// 对应 Rust 的 RknpuSubmit
typedef struct
{
    uint32_t           flags;
    uint32_t           timeout;
    uint32_t           task_start;
    uint32_t           task_number;
    uint32_t           task_counter;
    int32_t            priority;
    uint64_t           task_obj_addr;
    uint64_t           regcfg_obj_addr;
    uint64_t           task_base_addr;
    uint64_t           user_data;
    uint32_t           core_mask;
    int32_t            fence_fd;
    npu_subcore_task_t subcore_task[5];
} npu_submit_t;

void
rknpu_init(void);

extern void
rknpu_test();


#endif  // __RKNPU_H__
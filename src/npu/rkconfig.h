
#ifndef __RKNPU_REGS_H__
#define __RKNPU_REGS_H__

#include "t_types.h"

/*
 * ==========================================
 *  Rockchip NPU 寄存器基地址定义
 * ==========================================
 */

/// NPU 核心寄存器基地址
#define NPU0_BASE 0xFDAB0000UL
#define NPU1_BASE 0xFDAC0000UL
#define NPU2_BASE 0xFDAD0000UL
#define NPU0_IRQ  (110 + 32)
#define NPU1_IRQ  (111 + 32)
#define NPU2_IRQ  (112 + 32)

/// 每个核心的寄存器空间大小 (64KB)
#define NPU_CORE_SIZE 0x00010000UL

/// PMU1 (电源管理单元) 基地址
/// 来自设备树: power-management@fd8d8000
/// 用于控制 NPU 各核心的电源域开关
#define PMU1_BASE 0xFD8D8000UL

/// CRU (时钟复位单元) 基地址
/// 用于控制 NPU 时钟门控和软复位
#define CRU_BASE 0xFD7C0000UL

/// GPIO3 基地址
/// 用于某些电源控制引脚的 GPIO 操作
#define GPIO3_BASE 0xFEC40000UL

#define RK3588_NPU_VERSION 0x46495245UL  // 1179210309

#define INT_CLEAR_VALUE 0x1ffff

typedef struct
{
    /// 带宽优先级寄存器地址
    uint32_t bw_priority_addr;
    /// 带宽优先级寄存器长度
    uint32_t bw_priority_length;
    /// DMA 掩码位数
    uint32_t dma_mask_bits;
    /// PC 数据量缩放比例
    uint32_t pc_data_amount_scale;
    /// PC 任务编号位数
    uint32_t pc_task_number_bits;
    /// PC 任务编号掩码
    uint32_t pc_task_number_mask;
    /// PC 任务状态偏移
    uint32_t pc_task_status_offset;
    /// PC DMA 控制
    uint32_t pc_dma_ctrl;
    /// 带宽使能
    bool bw_enable;
    /// 中断数量
    size_t num_irqs;
    /// 复位数量
    size_t num_resets;
    /// NBUF 物理地址
    uint64_t nbuf_phyaddr;
    /// NBUF 大小
    uint64_t nbuf_size;
    /// 最大提交数量
    uint64_t max_submit_number;
    /// 核心掩码
    uint32_t core_mask;
} RknpuConfig;


#endif /* __RKNPU_REGS_H__ */

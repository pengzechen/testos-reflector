

#include "rkconfig.h"
#include "rknpu.h"
#include "lib/t_logger.h"
#include "t_mmio.h"
#include "t_exception.h"
#include "t_gicv3.h"
#include "rkpm.h"

// 只初始化一个RKNPU设备


/*
 * ==========================================
 *  RK3588 平台 NPU 配置
 * ==========================================
 */
static const RknpuConfig RK3588_CONFIG = {
    .bw_priority_addr      = 0x0,
    .bw_priority_length    = 0x0,
    .dma_mask_bits         = 40,
    .pc_data_amount_scale  = 2,
    .pc_task_number_bits   = 12,
    .pc_task_number_mask   = 0xFFF,
    .pc_task_status_offset = 0x3C,
    .pc_dma_ctrl           = 0,
    .bw_enable             = false,
    .num_irqs              = 3,
    .num_resets            = 3,
    .nbuf_phyaddr          = 0,
    .nbuf_size             = 0,
    .max_submit_number     = (1ULL << 12) - 1,
    .core_mask             = 0x7,
};

// ========== 私有函数定义 ==============

static void
rknpu_validate_version();

static void
job_done();

// ========== 公有函数定义 ==============

static void
rknpu_validate_version()
{
    // 读取版本寄存器
    uint32_t version     = read32((void *) (NPU0_BASE + RKNPU_VERSION));
    uint32_t version_num = read32((void *) (NPU0_BASE + RKNPU_VERSION_NUM));
    logger_info("RKNPU Version: %d\n", version);
    logger_info("RKNPU Version Num: %d\n", version_num);

    if (version != RK3588_NPU_VERSION) {
        logger_error("RKNPU: Unexpected version: 0x%x\n", version);
    } else {
        logger_info("RKNPU: Version validated successfully.\n");
    }
}

void
rknpu_init(void)
{
    logger_info("RKNPU: Initializing...\n");
    // 初始化代码

    struct rkpm_t pm;
    rockchip_pm_init(&pm, (void *) (uint64_t) PMU1_BASE);
    rockchip_pm_power_domain_on(&pm, PD_NPU);
    rockchip_pm_power_domain_on(&pm, PD_NPUTOP);
    rockchip_pm_power_domain_on(&pm, PD_NPU1);

    logger_info("RKNPU: Power domains enabled.\n");

    // 清除中断
    write32(INT_CLEAR_VALUE, (void *) (NPU0_BASE + RKNPU_INT_CLEAR));

    rknpu_validate_version();

    // 安装中断处理
    irq_install(NPU0_IRQ, job_done);

    gicv3_enable_int(NPU0_IRQ, true);

    if (gicv3_is_int_enabled(NPU0_IRQ)) {
        logger_warn("NPU0 IRQ %d is enabled in GICv3\n", NPU0_IRQ);
    }
}

static void
job_done()
{
    logger_info("RKNPU: Job completed.\n");
}

void
job_commit_pc(void    *task_ptr,
              void    *task_ptr_phys,
              uint32_t task_start,
              uint32_t task_number,
              uint32_t core,
              uint32_t flags)
{
    npu_task_t *task_base  = task_ptr;
    npu_task_t *first_task = NULL;
    npu_task_t *last_task  = NULL;

    int task_end = task_start + task_number - 1;
    first_task   = &task_base[task_start];
    last_task    = &task_base[task_end];

    int pc_data_amount_scale = RK3588_CONFIG.pc_data_amount_scale;
    int task_pp_en           = flags & RKNPU_JOB_PINGPONG ? 1 : 0;
    int pc_task_number_bits  = RK3588_CONFIG.pc_task_number_bits;

    logger_info("RKNPU: Committed PC job: task_start=%d, task_number=%d\n",
                task_start,
                task_number);
    logger_info("RKNPU: First task regcmd_addr=0x%llx, regcfg_amount=%d\n",
                first_task->regcmd_addr,
                first_task->regcfg_amount);


    // 写regcmd地址和数据量
    write32(first_task->regcmd_addr, (void *) (NPU0_BASE + RKNPU_PC_DATA_ADDR));
    uint32_t data_amount =
        (first_task->regcfg_amount + RKNPU_PC_DATA_EXTRA_AMOUNT + pc_data_amount_scale - 1) /
        pc_data_amount_scale;
    write32(data_amount, (void *) (NPU0_BASE + RKNPU_PC_DATA_AMOUNT));

    // 写intmask
    write32(last_task->int_mask, (void *) (NPU0_BASE + RKNPU_INT_MASK));
    write32(first_task->int_mask, (void *) (NPU0_BASE + RKNPU_INT_MASK));

    // 写task控制
    uint32_t pc_task_control = ((0x6 | task_pp_en) << pc_task_number_bits) | task_number;
    write32(pc_task_control, (void *) (NPU0_BASE + RKNPU_PC_TASK_CONTROL));

    // 写task_base_addr
    write32((uint32_t) (uint64_t) task_ptr_phys, (void *) (NPU0_BASE + RKNPU_PC_DMA_BASE_ADDR));

    //提交

    write32(0x1, (void *) (NPU0_BASE + RKNPU_PC_OP_EN));
    write32(0x0, (void *) (NPU0_BASE + RKNPU_PC_OP_EN));

    return;
}

void
job_wait_complete(uint32_t core, uint32_t task_number, uint32_t tmo)
{}


void
rknpu_submit_task(npu_submit_t *submit)
{
    uint32_t flags       = submit->flags;
    uint32_t tmo         = submit->timeout;
    uint32_t task_start  = submit->task_start;
    uint32_t task_number = submit->task_number;

    // 这里假设只使用核心0
    uint32_t core = 0x1;

    void *task_ptr      = (void *) (uint64_t) submit->task_obj_addr;
    void *task_ptr_phys = (void *) (uint64_t) submit->task_obj_addr;


    job_commit_pc(task_ptr, task_ptr_phys, task_start, task_number, core, flags);

    job_wait_complete(core, task_number, tmo);
}

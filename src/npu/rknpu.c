

#include "rkconfig.h"
#include "rknpu.h"
#include "lib/t_logger.h"
#include "dev/t_dw_uart.h"
#include "lib/t_string.h"
#include "mem/t_mmio.h"
#include "t_exception.h"
#include "dev/t_gicv3.h"
#include "rkpm.h"
#include "dev/t_timer.h"

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

static int job_done_num = 0;

// ========== 公有函数定义 ==============

#define NPU_REG_NUM 112


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
    rockchip_pm_power_domain_on(&pm, PD_NPU2);

    logger_info("RKNPU: Power domains enabled.\n");

    // 清除中断
    write32(INT_CLEAR_VALUE, (void *) (NPU0_BASE + RKNPU_INT_CLEAR));

    rknpu_validate_version();

    // 安装中断处理
    irq_install(NPU0_IRQ, job_done);

    // gicv3_enable_int(NPU0_IRQ, true);

    if (gicv3_is_int_enabled(NPU0_IRQ)) {
        logger_warn("NPU0 IRQ %d is enabled in GICv3\n", NPU0_IRQ);
    }
}

static inline uint32_t
rknpu_fuzz_status(uint32_t status)
{
    uint32_t fuzz_status = 0;

    if ((status & 0x3) != 0)
        fuzz_status |= 0x3;

    if ((status & 0xc) != 0)
        fuzz_status |= 0xc;

    if ((status & 0x30) != 0)
        fuzz_status |= 0x30;

    if ((status & 0xc0) != 0)
        fuzz_status |= 0xc0;

    if ((status & 0x300) != 0)
        fuzz_status |= 0x300;

    if ((status & 0xc00) != 0)
        fuzz_status |= 0xc00;

    return fuzz_status;
}

static void
job_done()
{
    logger("RKNPU: irq handler\n");
    job_done_num++;

    uint32_t status;
    status = read32((void *) (NPU0_BASE + RKNPU_INT_STATUS));

    uint32_t task_counter = status & RK3588_CONFIG.pc_task_number_mask;
    uint32_t fuzz         = rknpu_fuzz_status(status);
    logger_info("fuzz: %x\n", fuzz);
    // 0x300 come from task.int_mask
    if (fuzz != 0x300) {
        logger_error("invalid irq status: 0x%x\n", status);
        logger_error("raw status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_RAW_STATUS)));
        logger_error("task counter: %d\n", task_counter);
        write32(INT_CLEAR_VALUE, (void *) (NPU0_BASE + RKNPU_INT_CLEAR));
        return;
    }


    logger_info("status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_STATUS)));
    logger_info("row status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_RAW_STATUS)));
    write32(INT_CLEAR_VALUE, (void *) (NPU0_BASE + RKNPU_INT_CLEAR));
    logger("RKNPU: Job completed, times: %d.\n", job_done_num);
}

void
show_task_status(char *file, int line)
{
    uint32_t status;
    status = read32((void *) (NPU0_BASE + RKNPU_PC_TASK_STATUS));
    logger_info("npu0 task status: 0x%x, %s:%d\n", status, file, line);
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

    logger_info("first task addr: %p, last task addr: %p\n", first_task, last_task);
    logger_info("int_mask: 0x%x, int_clear: 0x%x\n", first_task->int_mask, first_task->int_clear);

    // switch to slave mode
    write32(0x1, (void *) (NPU0_BASE + RKNPU_PC_DATA_ADDR));


    write32((0xe + 0x10000000 * 0), (void *) (NPU0_BASE + (0x1004)));
    write32((0xe + 0x10000000 * 0), (void *) (NPU0_BASE + (0x3004)));

    logger_info("reg addr: 0x%x\n", first_task->regcmd_addr);
    // dump_reg(first_task->regcmd_addr, NPU_REG_NUM);
    // 写regcmd地址和数据量
    write32(first_task->regcmd_addr, (void *) (NPU0_BASE + RKNPU_PC_DATA_ADDR));
    uint32_t data_amount =
        (first_task->regcfg_amount + RKNPU_PC_DATA_EXTRA_AMOUNT + pc_data_amount_scale - 1) /
            pc_data_amount_scale -
        1;
    logger_info("data amount: %d\n", data_amount);
    write32(data_amount, (void *) (NPU0_BASE + RKNPU_PC_DATA_AMOUNT));

    // 写intmask
    write32(last_task->int_mask, (void *) (NPU0_BASE + RKNPU_INT_MASK));
    write32(first_task->int_mask, (void *) (NPU0_BASE + RKNPU_INT_CLEAR));


    // 写task控制
    uint32_t pc_task_control = ((0x6 | task_pp_en) << pc_task_number_bits) | task_number;
    logger_info("pc task control: 0x%x\n", pc_task_control);
    write32(pc_task_control, (void *) (NPU0_BASE + RKNPU_PC_TASK_CONTROL));

    // 写task_base_addr
    // 反编译demo写的是0
    // write32(0, (void *) (NPU0_BASE + RKNPU_PC_DMA_BASE_ADDR));

    //提交
    write32(0x1, (void *) (NPU0_BASE + RKNPU_PC_OP_EN));
    write32(0x0, (void *) (NPU0_BASE + RKNPU_PC_OP_EN));

    return;
}

bool
check_job_done()
{
    if (job_done_num != 0) {
        return true;
    }
    return false;
}

bool
check_job_done_noirq()
{
    uint32_t status;
    status        = read32((void *) (NPU0_BASE + RKNPU_INT_STATUS));
    uint32_t fuzz = rknpu_fuzz_status(status);
    if (fuzz == 0x300) {
        logger_info("status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_STATUS)));
        logger_info("row status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_RAW_STATUS)));
        return true;
    }
    return false;
}

void
job_wait_complete(uint32_t core, uint32_t task_number, uint32_t tmo)
{
    (void) core;
    int l = 0;
    do {
        // 简单轮询中断状态寄存器
        if (check_job_done_noirq()) {
            logger_info("wait loop: %d\n", l);
            logger_info("RKNPU: Job completed successfully.\n");
            return;
        } else {
            timer_delay_ms(1);
            tmo--;
            l++;
        }
    } while (tmo > 0);
    logger_error("RKNPU: Job wait timeout.\n");
    return;
}


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

    show_task_status(__FILE__, __LINE__);
    logger_info("status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_STATUS)));
    logger_info("row status: 0x%x\n", read32((void *) (NPU0_BASE + RKNPU_INT_RAW_STATUS)));

    job_commit_pc(task_ptr, task_ptr_phys, task_start, task_number, core, flags);

    show_task_status(__FILE__, __LINE__);

    job_wait_complete(core, task_number, tmo);
}

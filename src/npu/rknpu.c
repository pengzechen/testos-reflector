

#include "rkconfig.h"
#include "rknpu.h"
#include "lib/t_logger.h"
#include "t_mmio.h"

#include "rkpm.h"

// 只初始化一个RKNPU设备

#define RKNPU_JOB_PC       (1 << 0)
#define RKNPU_JOB_BLOCK    (0 << 1)
#define RKNPU_JOB_PINGPONG (1 << 2)


static void
rknpu_validate_version();

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
}

void
job_commit_pc(void    *task_ptr,
              void    *task_base_phys,
              uint32_t task_start,
              uint32_t task_number,
              uint32_t core_mask)
{

}

void job_wait_complete(uint32_t core_mask,
                      uint32_t task_number,
                      uint32_t tmo)
{

}


void
rknpu_submit_task(void    *task_ptr,
                  void    *task_base_phys,
                  uint32_t task_start,
                  uint32_t task_number,
                  uint32_t core_mask,
                  uint32_t tmo)
{
    job_commit_pc(task_ptr, task_base_phys, task_start, task_number, core_mask);

    job_wait_complete(core_mask, task_number, tmo);
}


void
rknpu_test()
{
    // 准备task
    npu_task_t task;
    task.flags       = 0;
    task.op_idx      = 0;
    task.enable_mask = 0x1;
    task.int_mask    = 0x300;  // Wait for DPU to finish
    task.int_clear   = INT_CLEAR_VALUE;
    task.int_status  = 0;

    task.regcfg_amount = 0x0;  // TODO
    task.regcfg_offset = 0;
    task.regcmd_addr   = 0x0;  // TODO

    // 准备submit结构
    npu_submit_t submit;
    submit.flags        = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG;
    submit.timeout      = 1000;  // 1秒超时
    submit.task_start   = 0;
    submit.task_number  = 1;
    submit.task_counter = 0;
    submit.priority     = 0;

    submit.task_obj_addr               = (uint64_t) (void *) &task;
    submit.task_base_addr              = (uint64_t) (void *) &task;
    submit.user_data                   = 0;
    submit.core_mask                   = 0x1;  // 使用核心0
    submit.fence_fd                    = -1;   // 不使用fence
    submit.subcore_task[0].task_start  = 0;
    submit.subcore_task[0].task_number = 1;


    rknpu_submit_task(&task,
                      &task,
                      submit.task_start,
                      submit.task_number,
                      submit.core_mask,
                      submit.timeout);
}
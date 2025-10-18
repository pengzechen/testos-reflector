

#include "rkconfig.h"
#include "rknpu.h"
#include "lib/t_logger.h"
#include "t_mmio.h"

#include "rkpm.h"

// 只初始化一个RKNPU设备



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



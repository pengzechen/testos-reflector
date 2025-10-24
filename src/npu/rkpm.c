#include "rkpm.h"
#include "t_sysreg.h"

#define MAX_WAIT_COUNT 10000


const pmu_info_t rk3588_pmu_info = {
    .pwr_offset           = 0x14c,
    .status_offset        = 0x180,
    .req_offset           = 0x10c,
    .idle_offset          = 0x120,
    .ack_offset           = 0x118,
    .mem_pwr_offset       = 0x1a0,
    .chain_status_offset  = 0x1f0,
    .mem_status_offset    = 0x1f8,
    .repair_status_offset = 0x290,

    .clk_ungate_offset          = 0,
    .mem_sd_offset              = 0,
    .core_pwrcnt_offset         = 0,
    .gpu_pwrcnt_offset          = 0,
    .core_power_transition_time = 0,
    .gpu_power_transition_time  = 0,

    .domains     = rk3588_domains,
    .num_domains = sizeof(rk3588_domains) / sizeof(rk3588_domains[0]),
};

/* 创建 rkpm_t 实例 */
void rockchip_pm_init(struct rkpm_t *pm, void *base)
{
    pm->reg = (struct pmu_regs_t){ .base = base };
    pm->info = &rk3588_pmu_info;
}

/* 打开电源域 */
int rockchip_pm_power_domain_on(struct rkpm_t *pm, uint32_t domain)
{
    return rockchip_pm_set_domain(pm, domain, true);
}

/* 关闭电源域 */
int rockchip_pm_power_domain_off(struct rkpm_t *pm, uint32_t domain)
{
    return rockchip_pm_set_domain(pm, domain, false);
}

/* 设置电源域状态 */
int rockchip_pm_set_domain(struct rkpm_t *pm, uint32_t domain, bool power_on)
{
    const domain_info_t *info = rockchip_pmu_get_domain(domain);

    if (info->pwr_mask == 0)
        return 0;

    rockchip_pm_write_power_control(pm, info, power_on);
    return rockchip_pm_wait_stable(pm, info, power_on);
}

/* 写电源控制寄存器 */
void rockchip_pm_write_power_control(struct rkpm_t *pm,
                                     const domain_info_t *info,
                                     bool power_on)
{
    uint32_t offset = pm->info->pwr_offset + info->pwr_offset;
    uint32_t val;

    if (info->pwr_w_mask != 0) {
        /* 写掩码方式 */
        val = power_on ? info->pwr_w_mask : (info->pwr_mask | info->pwr_w_mask);
        pmu_write32(&pm->reg, offset, val);
    } else {
        /* 读改写方式 */
        uint32_t cur = pmu_read32(&pm->reg, offset);
        uint32_t new_val = power_on ? (cur & ~info->pwr_mask) : (cur | info->pwr_mask);
        pmu_write32(&pm->reg, offset, new_val);
    }

    DSB_SY();
}

/* 等待电源域状态稳定 */
int rockchip_pm_wait_stable(struct rkpm_t *pm,
                            const domain_info_t *info,
                            bool expected_on)
{
    for (int i = 0; i < MAX_WAIT_COUNT; i++) {
        bool on = rockchip_pm_is_domain_on(pm, info);
        if (on == expected_on)
            return 0;
    }
    return -NPU_ERR_TIMEOUT;
}

/* 检查电源域状态 */
bool rockchip_pm_is_domain_on(struct rkpm_t *pm, const domain_info_t *info)
{
    uint32_t val;

    if (info->repair_status_mask != 0) {
        val = pmu_read32(&pm->reg, pm->info->repair_status_offset);
        return (val & info->repair_status_mask) != 0;
    }

    if (info->status_mask != 0) {
        val = pmu_read32(&pm->reg, pm->info->status_offset);
        return (val & info->status_mask) == 0; /* 0 表示开 */
    }

    /* 否则根据 idle 状态判断 */
    val = pmu_read32(&pm->reg, pm->info->idle_offset);
    return (val & info->idle_mask) != info->idle_mask;
}

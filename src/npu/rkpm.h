
#ifndef __PMU_PWR_CON0_T_H__
#define __PMU_PWR_CON0_T_H__

#include "t_types.h"
#include "t_mmio.h"

#define NPU_ERR_TIMEOUT -110

#define BIT(n) (1U << (n))


#define PD_NPU    0
#define PD_NPUTOP 1
#define PD_NPU1   2
#define PD_NPU2   3

//--------------------------------------
// Rockchip 电源域信息结构
//--------------------------------------
typedef struct
{
    const char *name;

    int32_t pwr_mask;
    int32_t status_mask;
    int32_t req_mask;
    int32_t idle_mask;
    int32_t ack_mask;
    bool    active_wakeup;

    int32_t pwr_w_mask;
    int32_t req_w_mask;
    int32_t mem_status_mask;
    int32_t repair_status_mask;
    int32_t clk_ungate_mask;
    int32_t clk_ungate_w_mask;

    int32_t mem_num;
    bool    keepon_startup;
    bool    always_on;

    uint32_t pwr_offset;
    uint32_t mem_offset;
    uint32_t req_offset;
} domain_info_t;

//--------------------------------------
// Rockchip PMU 信息结构
//--------------------------------------
typedef struct
{
    uint32_t pwr_offset;
    uint32_t status_offset;
    uint32_t req_offset;
    uint32_t idle_offset;
    uint32_t ack_offset;
    uint32_t mem_pwr_offset;
    uint32_t chain_status_offset;
    uint32_t mem_status_offset;
    uint32_t repair_status_offset;
    uint32_t clk_ungate_offset;
    uint32_t mem_sd_offset;
    uint32_t core_pwrcnt_offset;
    uint32_t gpu_pwrcnt_offset;
    uint32_t core_power_transition_time;
    uint32_t gpu_power_transition_time;

    const domain_info_t *domains;
    size_t               num_domains;
} pmu_info_t;


#define DOMAIN_INFO(_name,                                                                         \
                    _pwr_offset,                                                                   \
                    _pwr,                                                                          \
                    _status,                                                                       \
                    _mem_offset,                                                                   \
                    _mem_status,                                                                   \
                    _repair_status,                                                                \
                    _req_offset,                                                                   \
                    _req,                                                                          \
                    _idle,                                                                         \
                    _wakeup)                                                                       \
    {                                                                                              \
        .name               = _name,                                                               \
        .pwr_offset         = _pwr_offset,                                                         \
        .mem_offset         = _mem_offset,                                                         \
        .req_offset         = _req_offset,                                                         \
        .pwr_mask           = _pwr,                                                                \
        .status_mask        = _status,                                                             \
        .mem_status_mask    = _mem_status,                                                         \
        .repair_status_mask = _repair_status,                                                      \
        .req_mask           = _req,                                                                \
        .idle_mask          = _idle,                                                               \
        .ack_mask           = _idle,                                                               \
        .pwr_w_mask         = ((_pwr) << 16),                                                      \
        .req_w_mask         = ((_req) << 16),                                                      \
        .active_wakeup      = _wakeup,                                                             \
        .keepon_startup     = false,                                                               \
        .always_on          = false,                                                               \
    }


//--------------------------------------
// 域定义（等价于 Rust 中的 map! {...}）
//--------------------------------------
static const domain_info_t rk3588_domains[] = {
    DOMAIN_INFO("npu", 0x0, BIT(1), BIT(1), 0x0, 0, 0, 0x0, 0, 0, false),
    DOMAIN_INFO("nputop", 0x0, BIT(3), 0, 0x0, BIT(11), BIT(2), 0x0, BIT(1), BIT(1), false),
    DOMAIN_INFO("npu1", 0x0, BIT(4), 0, 0x0, BIT(12), BIT(3), 0x0, BIT(2), BIT(2), false),
    DOMAIN_INFO("npu2", 0x0, BIT(5), 0, 0x0, BIT(13), BIT(4), 0x0, BIT(3), BIT(3), false),
};



static inline const domain_info_t *
rockchip_pmu_get_domain(uint32_t id)
{
    return &rk3588_domains[id];
}


typedef union {
    uint32_t value;
    struct
    {
        uint32_t POWERMODE0_EN : 1;         // [0] Power mode 0 enable
        uint32_t PMU1_PWR_BYPASS : 1;       // [1] Bypass PD_PMU1 power gating flow
        uint32_t PMU1_BUS_BYPASS : 1;       // [2] Bypass BIU_PMU1 idle flow
        uint32_t WAKEUP_BYPASS : 1;         // [3] Bypass waiting for wake up interrupt
        uint32_t PMIC_BYPASS : 1;           // [4] Bypass waiting for PMIC stability
        uint32_t RESET_BYPASS : 1;          // [5] Bypass wake up reset clear stability
        uint32_t FREQ_SWITCH_BYPASS : 1;    // [6] Bypass frequency switch stability
        uint32_t OSC_DIS_BYPASS : 1;        // [7] Bypass disable oscillator
        uint32_t PMU1_PWR_GATE_ENA : 1;     // [8] Enable power down PD_PMU1 by hardware
        uint32_t PMU1_PWR_GATE_SFTENA : 1;  // [9] Enable power down PD_PMU1 by software
        uint32_t PMU1_MEM_PWR_GATE_SFTENA
            : 1;                         // [10] Enable power down PD_PMU1's memory by software
        uint32_t PMU1_BUS_IDLE_ENA : 1;  // [11] Enable sending idle request to BIU_PMU1 by hardware
        uint32_t PMU1_BUS_IDLE_SFTENA
            : 1;                        // [12] Enable sending idle request to BIU_PMU1 by software
        uint32_t BIU_AUTO_PMU1 : 1;     // [13] BIU_PMU1 clock auto gate
        uint32_t POWER_OFF_IO_ENA : 1;  // [14] Enable VCCIO enter low power mode
        uint32_t RESERVED15 : 1;        // [15] Reserved (RO)
        uint32_t WRITE_ENABLE : 16;     // [31:16] Write enable for lower 16 bits
    } bits;
} PMU_PWR_CON0_t;


struct pmu_regs_t
{
    void *base;
};

struct rkpm_t
{
    struct pmu_regs_t  reg;
    const pmu_info_t *info;
};

/* 基础寄存器操作 */
static inline void
pmu_write32(struct pmu_regs_t *r, uint32_t off, uint32_t val)
{
    write32(val, r->base + off);
}

static inline uint32_t
pmu_read32(struct pmu_regs_t *r, uint32_t off)
{
    return read32(r->base + off);
}

/* 对外接口 */
void
rockchip_pm_init(struct rkpm_t *pm, void *base);
int
rockchip_pm_power_domain_on(struct rkpm_t *pm, uint32_t domain);
int
rockchip_pm_power_domain_off(struct rkpm_t *pm, uint32_t domain);
int
rockchip_pm_set_domain(struct rkpm_t *pm, uint32_t domain, bool power_on);
void
rockchip_pm_write_power_control(struct rkpm_t *pm, const domain_info_t *info, bool power_on);
int
rockchip_pm_wait_stable(struct rkpm_t *pm, const domain_info_t *info, bool expected_on);
bool
rockchip_pm_is_domain_on(struct rkpm_t *pm, const domain_info_t *info);

#endif  // __PMU_PWR_CON0_T_H__
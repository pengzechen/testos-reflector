
/*
power-management@fd8d8000 {
		compatible = "rockchip,rk3588-pmu\0syscon\0simple-mfd";
		reg = <0x00 0xfd8d8000 0x00 0x400>;
		phandle = <0xc2>;

		power-controller {
			compatible = "rockchip,rk3588-power-controller";
			#power-domain-cells = <0x01>;
			#address-cells = <0x01>;
			#size-cells = <0x00>;
			status = "okay";
			phandle = <0x52>;

			power-domain@8 {
				reg = <0x08>;
				#address-cells = <0x01>;
				#size-cells = <0x00>;

				power-domain@9 {
					reg = <0x09>;
					#address-cells = <0x01>;
					#size-cells = <0x00>;
					clocks = <0x02 0x12f 0x02 0x131 0x02 0x130 0x02 0x126>;
					pm_qos = <0x6f 0x70 0x71>;

					power-domain@10 {
						reg = <0x0a>;
						clocks = <0x02 0x12f 0x02 0x131 0x02 0x130>;
						pm_qos = <0x72>;
					};

					power-domain@11 {
						reg = <0x0b>;
						clocks = <0x02 0x12f 0x02 0x131 0x02 0x130>;
						pm_qos = <0x73>;
					};
				};
			};

			power-domain@12 {
				reg = <0x0c>;
				clocks = <0x02 0x114 0x02 0x115 0x02 0x116>;
				pm_qos = <0x74 0x75 0x76 0x77>;
			};

			power-domain@13 {
				reg = <0x0d>;
				#address-cells = <0x01>;
				#size-cells = <0x00>;

				power-domain@14 {
					reg = <0x0e>;
					clocks = <0x02 0x18f 0x02 0x1be 0x02 0x1bc 0x02 0x190 0x02 0x18e>;
					pm_qos = <0x78>;
				};

				power-domain@15 {
					reg = <0x0f>;
					clocks = <0x02 0x194 0x02 0x1be 0x02 0x1bc 0x02 0x195>;
					pm_qos = <0x79>;
				};

				power-domain@16 {
					reg = <0x10>;
					#address-cells = <0x01>;
					#size-cells = <0x00>;
					clocks = <0x02 0x1c4 0x02 0x1c5>;
					pm_qos = <0x7a 0x7b 0x7c>;

					power-domain@17 {
						reg = <0x11>;
						clocks = <0x02 0x1c9 0x02 0x1c4 0x02 0x1c5 0x02 0x1ca>;
						pm_qos = <0x7d 0x7e 0x7f>;
					};
				};
			};

			power-domain@21 {
				reg = <0x15>;
				#address-cells = <0x01>;
				#size-cells = <0x00>;
				clocks = <0x02 0x1be 0x02 0x1bd 0x02 0x1bc 0x02 0x1bf 0x02 0x1aa 0x02 0x1a9 0x02 0x1ac 0x02 0x1ad 0x02 0x1ae 0x02 0x1af 0x02 0x1b0 0x02 0x1b1 0x02 0x1b2 0x02 0x1b3 0x02 0x1b4 0x02 0x1b5 0x02 0x1b7 0x02 0x1b6>;
				pm_qos = <0x80 0x81 0x82 0x83 0x84 0x85 0x86 0x87>;

				power-domain@23 {
					reg = <0x17>;
					clocks = <0x02 0x4b 0x02 0x49 0x02 0x1be>;
					pm_qos = <0x88>;
				};

				power-domain@14 {
					reg = <0x0e>;
					clocks = <0x02 0x18f 0x02 0x1be 0x02 0x1bc 0x02 0x190>;
					pm_qos = <0x78>;
				};

				power-domain@15 {
					reg = <0x0f>;
					clocks = <0x02 0x194 0x02 0x1be 0x02 0x1bc>;
					pm_qos = <0x79>;
				};

				power-domain@22 {
					reg = <0x16>;
					clocks = <0x02 0x1ba 0x02 0x1b9>;
					pm_qos = <0x89>;
				};
			};

			power-domain@24 {
				reg = <0x18>;
				#address-cells = <0x01>;
				#size-cells = <0x00>;
				clocks = <0x02 0x26e 0x02 0x26d 0x02 0x270>;
				pm_qos = <0x8a 0x8b>;

				power-domain@25 {
					reg = <0x19>;
					clocks = <0x02 0x1f6 0x02 0x1f7 0x02 0x1f5 0x02 0x1f3 0x02 0x1ee 0x02 0x1ed 0x02 0x26d>;
					pm_qos = <0x8c>;
				};
			};

			power-domain@26 {
				reg = <0x1a>;
				clocks = <0x02 0x22e 0x02 0x22f 0x02 0x22d 0x02 0x218 0x02 0x217 0x02 0x22b 0x02 0x264>;
				pm_qos = <0x8d 0x8e>;
			};

			power-domain@27 {
				reg = <0x1b>;
				#address-cells = <0x01>;
				#size-cells = <0x00>;
				clocks = <0x02 0x1e1 0x02 0x1e2 0x02 0x1df 0x02 0x1de 0x02 0x1e5 0x02 0x1e4>;
				pm_qos = <0x8f 0x90 0x91 0x92>;

				power-domain@28 {
					reg = <0x1c>;
					clocks = <0x02 0x121 0x02 0x120 0x02 0x1e1 0x02 0x1e2>;
					pm_qos = <0x93 0x94>;
				};

				power-domain@29 {
					reg = <0x1d>;
					clocks = <0x02 0x1d6 0x02 0x1d5 0x02 0x1d9 0x02 0x1d8 0x02 0x1e2>;
					pm_qos = <0x95 0x96>;
				};
			};

			power-domain@30 {
				reg = <0x1e>;
				clocks = <0x02 0x189 0x02 0x18a>;
				pm_qos = <0x97>;
			};

			power-domain@31 {
				reg = <0x1f>;
				clocks = <0x02 0x166 0x02 0x19b 0x02 0x19c 0x02 0x19d 0x02 0x19e 0x02 0x19f 0x02 0x1a0>;
				pm_qos = <0x98 0x99 0x9a 0x9b>;
			};

			power-domain@33 {
				reg = <0x21>;
				clocks = <0x02 0x166 0x02 0x169 0x02 0x16a>;
			};

			power-domain@34 {
				reg = <0x22>;
				clocks = <0x02 0x166 0x02 0x169 0x02 0x16a>;
			};

			power-domain@37 {
				reg = <0x25>;
				clocks = <0x02 0x199 0x02 0x140>;
				pm_qos = <0x9c>;
			};

			power-domain@38 {
				reg = <0x26>;
				clocks = <0x02 0x3c 0x02 0x3d>;
			};

			power-domain@40 {
				reg = <0x28>;
				pm_qos = <0x9d>;
			};
		};
	};
*/

#ifndef __PMU_PWR_CON0_T_H__
#define __PMU_PWR_CON0_T_H__

#include "t_types.h"
#include "t_mmio.h"

#define NPU_ERR_TIMEOUT -110




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
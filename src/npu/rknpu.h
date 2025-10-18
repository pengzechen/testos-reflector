
/*
	npu@fdab0000 { 
		compatible = "rockchip,rk3588-rknpu";
		reg = <0x00 0xfdab0000 0x00 0x10000 0x00 0xfdac0000 0x00 0x10000 0x00 0xfdad0000 0x00 0x10000>;
		interrupts = <0x00 0x6e 0x04 0x00 0x6f 0x04 0x00 0x70 0x04>;
		interrupt-names = "npu0_irq\0npu1_irq\0npu2_irq";
		clocks = <0x0e 0x06 0x02 0x12d 0x02 0x122 0x02 0x124 0x02 0x12e 0x02 0x123 0x02 0x125 0x02 0x131>;
		clock-names = "clk_npu\0aclk0\0aclk1\0aclk2\0hclk0\0hclk1\0hclk2\0pclk";
		assigned-clocks = <0x0e 0x06>;
		assigned-clock-rates = <0xbebc200>;
		resets = <0x02 0x1e6 0x02 0x1b0 0x02 0x1c0 0x02 0x1e8 0x02 0x1b2 0x02 0x1c2>;
		reset-names = "srst_a0\0srst_a1\0srst_a2\0srst_h0\0srst_h1\0srst_h2";
		power-domains = <0x52 0x09 0x52 0x0a 0x52 0x0b>;
		power-domain-names = "npu0\0npu1\0npu2";
		operating-points-v2 = <0x9e>;
		iommus = <0x9f>;
		status = "okay";
		rknpu-supply = <0xa0>;
		mem-supply = <0xa0>;
		phandle = <0x23a>;
	};
*/

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


#define RKNPU_JOB_PC       (1 << 0)
#define RKNPU_JOB_BLOCK    (0 << 1)
#define RKNPU_JOB_PINGPONG (1 << 2)

#define RKNPU_PC_DATA_EXTRA_AMOUNT 4


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

void
rknpu_submit_task(npu_submit_t *submit);


#endif  // __RKNPU_H__
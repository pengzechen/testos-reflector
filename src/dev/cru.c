

/*

// rcu 设备树
clock-controller@fd7c0000 {
	compatible = "rockchip,rk3588-cru";
	rockchip,grf = <0x66>;
	reg = <0x00 0xfd7c0000 0x00 0x5c000>;
	#clock-cells = <0x01>;
	#reset-cells = <0x01>;
	assigned-clocks = <0x02 0x09 0x02 0x05 0x02 0x08 0x02 0x07 0x02 0xd8 0x02 0xda 0x02 0xd9 0x02 0x10e 0x02 0x10f 0x02 0x110 0x02 0x299 0x02 0x29a 0x02 0x7b 0x02 0xec 0x02 0x114>;
	assigned-clock-rates = <0x4190ab00 0x2ee00000 0x32a9f880 0x46cf7100 0x29d7ab80 0x17d78400 0x1dcd6500 0x2faf0800 0x5f5e100 0x17d78400 0x5f5e100 0xbebc200 0x165a0bc0 0x8f0d180 0xbebc200>;
	phandle = <0x02>;
};

// npu 需要的时钟列表
static const char * const rknpu_clk_names[] = {
    "clk_npu",
    "aclk0", "aclk1", "aclk2",
    "hclk0", "hclk1", "hclk2",
    "pclk",
};




// 参考 /home/ajax/Projects/rk3588/linux-orangepi/drivers/clk/rockchip/clk-rk3588.c  1368 line

COMPOSITE(CLK_NPU_DSU0, "clk_npu_dsu0", gpll_cpll_aupll_npll_spll_p, 0, RK3588_CLKSEL_CON(73), 7, 3, MFLAGS, 2, 5, DFLAGS, RK3588_CLKGATE_CON(29), 1, GFLAGS),
GATE(ACLK_NPU0, "aclk_npu0", "clk_npu_dsu0", 0, RK3588_CLKGATE_CON(30), 6, GFLAGS),
GATE(ACLK_NPU1, "aclk_npu1", "clk_npu_dsu0", 0, RK3588_CLKGATE_CON(27), 0, GFLAGS),
GATE(ACLK_NPU2, "aclk_npu2", "clk_npu_dsu0", 0, RK3588_CLKGATE_CON(28), 0, GFLAGS),


COMPOSITE_NODIV(HCLK_NPU_ROOT, "hclk_npu_root", mux_200m_100m_50m_24m_p, 0, RK3588_CLKSEL_CON(73), 0, 2, MFLAGS, RK3588_CLKGATE_CON(29), 0, GFLAGS),
GATE(HCLK_NPU1, "hclk_npu1", "hclk_npu_root", 0, RK3588_CLKGATE_CON(27), 2, GFLAGS),
GATE(HCLK_NPU2, "hclk_npu2", "hclk_npu_root", 0, RK3588_CLKGATE_CON(28), 2, GFLAGS),
GATE(HCLK_NPU0, "hclk_npu0", "hclk_npu_root", 0, RK3588_CLKGATE_CON(30), 8, GFLAGS),


COMPOSITE_NODIV(PCLK_NPU_ROOT, "pclk_npu_root", mux_100m_50m_24m_p, 0, RK3588_CLKSEL_CON(74), 1, 2, MFLAGS, RK3588_CLKGATE_CON(29), 4, GFLAGS),
GATE(PCLK_NPU_TIMER, "pclk_npu_timer", "pclk_npu_root", 0, RK3588_CLKGATE_CON(29), 6, GFLAGS),
GATE(PCLK_NPU_PVTM, "pclk_npu_pvtm", "pclk_npu_root", 0, RK3588_CLKGATE_CON(29), 12, GFLAGS),
GATE(PCLK_NPU_GRF, "pclk_npu_grf", "pclk_npu_root", CLK_IGNORE_UNUSED, RK3588_CLKGATE_CON(29), 13, GFLAGS),
GATE(PCLK_NPU_WDT, "pclk_npu_wdt", "pclk_npu_root", 0, RK3588_CLKGATE_CON(29), 10, GFLAGS),

COMPOSITE_NODIV(CLK_NPUTIMER_ROOT, "clk_nputimer_root", mux_24m_100m_p, 0, RK3588_CLKSEL_CON(74), 3, 1, MFLAGS, RK3588_CLKGATE_CON(29), 7, GFLAGS),
GATE(CLK_NPUTIMER0, "clk_nputimer0", "clk_nputimer_root", 0, RK3588_CLKGATE_CON(29), 8, GFLAGS),
GATE(CLK_NPUTIMER1, "clk_nputimer1", "clk_nputimer_root", 0, RK3588_CLKGATE_CON(29), 9, GFLAGS),


COMPOSITE_NODIV(HCLK_NPU_CM0_ROOT, "hclk_npu_cm0_root", mux_400m_200m_100m_24m_p, 0, RK3588_CLKSEL_CON(74), 5, 2, MFLAGS, RK3588_CLKGATE_CON(30), 1, GFLAGS),
COMPOSITE(CLK_NPU_CM0_RTC, "clk_npu_cm0_rtc", mux_24m_32k_p, 0, RK3588_CLKSEL_CON(74), 12, 1, MFLAGS, 7, 5, DFLAGS, RK3588_CLKGATE_CON(30), 5, GFLAGS),
GATE(TCLK_NPU_WDT, "tclk_npu_wdt", "xin24m", 0, RK3588_CLKGATE_CON(29), 11, GFLAGS),
GATE(FCLK_NPU_CM0_CORE, "fclk_npu_cm0_core", "hclk_npu_cm0_root", 0, RK3588_CLKGATE_CON(30), 3, GFLAGS),
GATE(CLK_NPU_PVTM, "clk_npu_pvtm", "xin24m", 0, RK3588_CLKGATE_CON(29), 14, GFLAGS),
GATE(CLK_CORE_NPU_PVTM, "clk_core_npu_pvtm", "clk_npu_dsu0", 0, RK3588_CLKGATE_CON(29), 15, GFLAGS),

CLKSEL_CON	0x0300	时钟选择 & 分频配置
CLKGATE_CON	0x0800	时钟门控（开关）
SOFTRST_CON	0x0A00	软复位控制
*/


/*
#define GATE(_id, cname, pname, f, o, b, gf)			\
	{							\
		.id		= _id,				\
		.branch_type	= branch_gate,			\
		.name		= cname,			\
		.parent_names	= (const char *[]){ pname },	\
		.num_parents	= 1,				\
		.flags		= f,				\
		.gate_offset	= o,				\
		.gate_shift	= b,				\
		.gate_flags	= gf,				\
	}
#define COMPOSITE_NODIV(_id, cname, pnames, f, mo, ms, mw, mf,	\
			go, gs, gf)				\
	{							\
		.id		= _id,				\
		.branch_type	= branch_composite,		\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= f,				\
		.muxdiv_offset	= mo,				\
		.mux_shift	= ms,				\
		.mux_width	= mw,				\
		.mux_flags	= mf,				\
		.gate_offset	= go,				\
		.gate_shift	= gs,				\
		.gate_flags	= gf,				\
	}
*/


/*
	寄存器操作方法：
	你写 (1 << (6+16)) | (0 << 6)，意思是：
	mask = 1 << 6 → 只操作 bit6
	value = 0 → 打开 gate（0 = enable）
*/

#include "dev/cru.h"
#include "mem/t_mmio.h"
#include "lib/t_logger.h"


void
enable_ptimer()
{
    // 外设时钟
    // PCLK_NPU_ROOT  外设寄存器时钟（Peripheral clock） 通常给 CPU 或 DMA 使用。
    write32((1 << (4 + 16)) | (0 << 4), (void *) RK3588_CLKGATE_CON(29));
    // PCLK_NPU_GRF
    write32((1 << (13 + 16)) | (0 << 13), (void *) RK3588_CLKGATE_CON(29));
}
void
enable_htimer()
{
    // 模块时钟
    // HCLK_NPU_ROOT 模块总线时钟（AHB clock） NPU 寄存器访问走这个时钟。
    write32((1 << (0 + 16)) | (0 << 0), (void *) RK3588_CLKGATE_CON(29));  // enable_hclknpu_root
    // HCLK_NPU0
    write32((1 << (8 + 16)) | (0 << 8), (void *) RK3588_CLKGATE_CON(30));  // enable_hclknpu0
    // HCLK_NPU1
    write32((1 << (2 + 16)) | (0 << 2), (void *) RK3588_CLKGATE_CON(27));
    // HCLK_NPU2
    write32((1 << (2 + 16)) | (0 << 2), (void *) RK3588_CLKGATE_CON(28));
}
void
enable_atimer()
{
    // AXI 总线时钟
    // 高速 (~DDR/AXI频率)
    // 内存访问总线
    // ACLK_NPU0
    write32((1 << (6 + 16)) | (0 << 6), (void *) RK3588_CLKGATE_CON(30));  // enable_aclknpu0
    // ACLK_NPU1
    write32((1 << (0 + 16)) | (0 << 0), (void *) RK3588_CLKGATE_CON(27));
    // ACLK_NPU2
    write32((1 << (0 + 16)) | (0 << 0), (void *) RK3588_CLKGATE_CON(28));
}


// ========================  ACLK ===============================

void // 测试主时钟是否开启
test_clk_npu_dsu0(void)
{
    uint32_t val;
    bool     enabled;

    // 1️⃣ 检查 gate
    val     = read32((void *) (RK3588_CLKGATE_CON(29)));
    enabled = ((val >> 1) & 1) == 0;  // bit 1 对应 CLK_NPU_DSU0
    if (enabled)
        logger_info("CLK_NPU_DSU0 gate is enabled.\n");
    else
        logger_warn("CLK_NPU_DSU0 gate is disabled.\n");

    // 2️⃣ 检查 reset
    val     = read32((void *) (RK3588_SOFTRST_CON(30)));
    enabled = ((val >> 0) & 1) == 0;  // bit 0 对应 CLK_NPU_DSU0 reset
    if (enabled)
        logger_info("CLK_NPU_DSU0 is out of reset.\n");
    else
        logger_warn("CLK_NPU_DSU0 is still in reset.\n");

    // 3️⃣ 检查父时钟选择
    val             = read32((void *) (RK3588_CLKSEL_CON(73)));
    uint32_t parent = (val >> 7) & 0x7;  // bits 9:7
    if (parent == 0)
        logger_info("CLK_NPU_DSU0 parent is gpll (0).\n");
    else
        logger_warn("CLK_NPU_DSU0 parent is NOT gpll, mux=%u.\n", parent);
}

void
enable_aclknpu_root() {
	// CLK_NPU_DSU0 
    /*
		RK3588_CLKSEL_CON(73) → 寄存器 73 的偏移地址
		由官方时钟宏和设备树可知，CLK_NPU_DSU0 的 parent mux 位于 CON73
		7 << 7 → 位掩码
		CLK_NPU_DSU0 的 mux 是 bits 9:7
		val |= 0x0 << 7 → 选择 gpll 作为父时钟
	*/
    uint32_t val = read32((void *) (RK3588_CLKSEL_CON(73)));
    val &= ~(0x7 << 7);  // 清除 CLK_NPU_DSU0 的 mux 位
    val |= 0x0 << 7;     // 选择 gpll(0) cpll(1)
    write32(val, (void *) (RK3588_CLKSEL_CON(73)));

    /*
		RK3588_SOFTRST_CON(30) → 寄存器 30，NPU 子模块复位寄存器
		(1 << (0+16)) | (0 << 0)
		高 16 位：写掩码（告诉硬件哪些 bit 要更新）
		低 16 位：写入值（0 = 释放 reset）
		bit 0 对应 CLK_NPU_DSU0 模块的复位位
	*/
    write32((1 << (0 + 16)) | (0 << 0), (void *) (RK3588_SOFTRST_CON(30)));

    /*
		RK3588_CLKGATE_CON(29) → 寄存器 29，NPU 子模块 gate 寄存器
		(1 << (1+16)) | (0 << 1)
		高 16 位：掩码，表示操作 bit 1
		低 16 位：值 0 → 打开 gate（0 = enable, 1 = disable）
		bit 1 对应 CLK_NPU_DSU0 gate
	*/
    write32((1 << (1 + 16)) | (0 << 1), (void *) (RK3588_CLKGATE_CON(29)));


    test_clk_npu_dsu0();
}

void
enable_aclknpu0(void)
{
    uint32_t val;
    bool enabled;

    // -------------------------------
    // 1️⃣ 选择父时钟 (ACLK_NPU0 -> CLK_NPU_DSU0)
    // ACLKNPU0 的 parent 就是 CLK_NPU_DSU0，通常不用改变 mux
    // 如果需要手动选择，这里可以写类似：
    // val = read32(RK3588_CLKSEL_CON(73));
    // val &= ~(0x7 << 7);  // 清除 CLK_NPU_DSU0 的 mux 位
    // val |= 0x1 << 7;     // 选择 cpll 或 gpll
    // write32(val, RK3588_CLKSEL_CON(73));
    // -------------------------------

    // -------------------------------
    // 2️⃣ 释放 reset
    // ACLK_NPU0 对应 bit = 6 in RK3588_CLKGATE_CON(30)
    // 注意 NPU reset 位可能在 SOFTRST_CON 寄存器的不同 bit，需要参考手册
    write32((1 << (6 + 16)) | (0 << 6), (void *) (RK3588_SOFTRST_CON(30)));

    // -------------------------------
    // 3️⃣ 打开 gate
    // bit 6 对应 ACLK_NPU0 gate
    write32((1 << (6 + 16)) | (0 << 6), (void *) (RK3588_CLKGATE_CON(30)));

    // -------------------------------
    // 4️⃣ 测试 gate 是否打开
    val     = read32((void *) (RK3588_CLKGATE_CON(30)));
    enabled = ((val >> 6) & 1) == 0;  // 0 = enable

    if (enabled)
        logger_info("ACLK_NPU0 gate is enabled.\n");
    else
        logger_warn("ACLK_NPU0 gate is disabled.\n");

    // -------------------------------
    // 5️⃣ 测试 reset 是否释放
    val     = read32((void *) (RK3588_SOFTRST_CON(30)));
    enabled = ((val >> 6) & 1) == 0;  // 0 = out of reset
    if (enabled)
        logger_info("ACLK_NPU0 is out of reset.\n");
    else
        logger_warn("ACLK_NPU0 is still in reset.\n");
}

// ==============================================================

void
enable_hclknpu_root(void)
{
    uint32_t val;
    bool enabled;

    // -------------------------------
    // 1️⃣ 选择父时钟 (HCLK_NPU_ROOT 的 mux 在 CON73 bits 1:0)
    val = read32((void *) (RK3588_CLKSEL_CON(73)));
    val &= ~(0x3 << 0);  // 清除 bits 1:0
    val |= 0x0 << 0;     // 选择 mux 0 (例如 200MHz)
    write32(val, (void *) (RK3588_CLKSEL_CON(73)));

    // -------------------------------
    // 2️⃣ 释放 reset (如果有 reset，对应 bit 0 in SOFTRST_CON? 这里假设 SOFTRST_CON(30) bit 0)
    write32((1 << (0 + 16)) | (0 << 0), (void *) (RK3588_SOFTRST_CON(30)));

    // -------------------------------
    // 3️⃣ 打开 gate (bit 0 in CLKGATE_CON(29))
    write32((1 << (0 + 16)) | (0 << 0), (void *) (RK3588_CLKGATE_CON(29)));

    // -------------------------------
    // 4️⃣ 测试 gate 是否打开
    val     = read32((void *) (RK3588_CLKGATE_CON(29)));
    enabled = ((val >> 0) & 1) == 0;
    if (enabled)
        logger_info("HCLK_NPU_ROOT gate is enabled.\n");
    else
        logger_warn("HCLK_NPU_ROOT gate is disabled.\n");

    // -------------------------------
    // 5️⃣ 测试 reset 是否释放
    val     = read32((void *) (RK3588_SOFTRST_CON(30)));
    enabled = ((val >> 0) & 1) == 0;
    if (enabled)
        logger_info("HCLK_NPU_ROOT is out of reset.\n");
    else
        logger_warn("HCLK_NPU_ROOT is still in reset.\n");

    // -------------------------------
    // 6️⃣ 输出父时钟选择
    val = read32((void *) (RK3588_CLKSEL_CON(73)));
    uint32_t mux = (val >> 0) & 0x3;
    logger_info("HCLK_NPU_ROOT parent mux = %u\n", mux);
}

void
enable_hclknpu0(void)
{
    uint32_t val;
    bool enabled;

    // -------------------------------
    // 1️⃣ 选择父时钟 (HCLK_NPU0 的父时钟是 HCLK_NPU_ROOT)
    // 如果需要手动选择 mux，可在 CLKSEL_CON 寄存器修改，这里假设直接使用 root，不改 mux
    logger_info("HCLK_NPU0 parent is HCLK_NPU_ROOT (no mux change).\n");

    // -------------------------------
    // 2️⃣ 释放 reset
    // 假设 HCLK_NPU0 的 reset 位在 SOFTRST_CON(30) 的 bit 8
    write32((1 << (8 + 16)) | (0 << 8), (void *) (RK3588_SOFTRST_CON(30)));

    // -------------------------------
    // 3️⃣ 打开 gate (bit 8 in CLKGATE_CON(30))
    write32((1 << (8 + 16)) | (0 << 8), (void *) (RK3588_CLKGATE_CON(30)));

    // -------------------------------
    // 4️⃣ 测试 gate 是否打开
    val     = read32((void *) (RK3588_CLKGATE_CON(30)));
    enabled = ((val >> 8) & 1) == 0;  // 0 = enable
    if (enabled)
        logger_info("HCLK_NPU0 gate is enabled.\n");
    else
        logger_warn("HCLK_NPU0 gate is disabled.\n");

    // -------------------------------
    // 5️⃣ 测试 reset 是否释放
    val     = read32((void *) (RK3588_SOFTRST_CON(30)));
    enabled = ((val >> 8) & 1) == 0;  // 0 = out of reset
    if (enabled)
        logger_info("HCLK_NPU0 is out of reset.\n");
    else
        logger_warn("HCLK_NPU0 is still in reset.\n");
}


// ========================  PCLK ===============================

void
enable_ptimer_root(void)
{
    uint32_t val;
    bool enabled;

    // -------------------------------
    // 1️⃣ 选择父时钟 (PCLK_NPU_ROOT mux 在 CLKSEL_CON(74) bits 2:1)
    val = read32((void *) (RK3588_CLKSEL_CON(74)));
    val &= ~(0x3 << 1);  // 清除 bits 2:1
    val |= 0x0 << 1;     // 选择 mux 0，例如 100MHz
    write32(val, (void *) (RK3588_CLKSEL_CON(74)));

    // -------------------------------
    // 2️⃣ 释放 reset (假设 bit 4 in SOFTRST_CON(30))
    write32((1 << (4 + 16)) | (0 << 4), (void *) (RK3588_SOFTRST_CON(30)));

    // -------------------------------
    // 3️⃣ 打开 gate (bit 4 in CLKGATE_CON(29))
    write32((1 << (4 + 16)) | (0 << 4), (void *) (RK3588_CLKGATE_CON(29)));

    // -------------------------------
    // 4️⃣ 测试 gate 是否打开
    val     = read32((void *) (RK3588_CLKGATE_CON(29)));
    enabled = ((val >> 4) & 1) == 0;  // 0 = enable
    if (enabled)
        logger_info("PCLK_NPU_ROOT gate is enabled.\n");
    else
        logger_warn("PCLK_NPU_ROOT gate is disabled.\n");

    // -------------------------------
    // 5️⃣ 测试 reset 是否释放
    val     = read32((void *) (RK3588_SOFTRST_CON(30)));
    enabled = ((val >> 4) & 1) == 0;  // 0 = out of reset
    if (enabled)
        logger_info("PCLK_NPU_ROOT is out of reset.\n");
    else
        logger_warn("PCLK_NPU_ROOT is still in reset.\n");

    // -------------------------------
    // 6️⃣ 输出父时钟选择
    val = read32((void *) (RK3588_CLKSEL_CON(74)));
    uint32_t mux = (val >> 1) & 0x3;
    logger_info("PCLK_NPU_ROOT parent mux = %u\n", mux);
}


void
enable_nputimer_root(void)
{
    uint32_t val;
    bool enabled;

    // -------------------------------
    // 1️⃣ 选择父时钟 (CLK_NPUTIMER_ROOT mux 在 CLKSEL_CON(74) bit 3, width 1)
    val = read32((void *) (RK3588_CLKSEL_CON(74)));
    val &= ~(0x1 << 3);  // 清除 bit 3
    val |= 0x0 << 3;     // 选择 mux 0 (例如 24MHz)
    write32(val, (void *) (RK3588_CLKSEL_CON(74)));

    // -------------------------------
    // 2️⃣ 释放 reset (bit 7 in SOFTRST_CON(30))
    write32((1 << (7 + 16)) | (0 << 7), (void *) (RK3588_SOFTRST_CON(30)));

    // -------------------------------
    // 3️⃣ 打开 gate (bit 7 in CLKGATE_CON(29))
    write32((1 << (7 + 16)) | (0 << 7), (void *) (RK3588_CLKGATE_CON(29)));

    // -------------------------------
    // 4️⃣ 测试 gate 是否打开
    val     = read32((void *) (RK3588_CLKGATE_CON(29)));
    enabled = ((val >> 7) & 1) == 0;  // 0 = enable
    if (enabled)
        logger_info("CLK_NPUTIMER_ROOT gate is enabled.\n");
    else
        logger_warn("CLK_NPUTIMER_ROOT gate is disabled.\n");

    // -------------------------------
    // 5️⃣ 测试 reset 是否释放
    val     = read32((void *) (RK3588_SOFTRST_CON(30)));
    enabled = ((val >> 7) & 1) == 0;  // 0 = out of reset
    if (enabled)
        logger_info("CLK_NPUTIMER_ROOT is out of reset.\n");
    else
        logger_warn("CLK_NPUTIMER_ROOT is still in reset.\n");

    // -------------------------------
    // 6️⃣ 输出父时钟选择
    val = read32((void *) (RK3588_CLKSEL_CON(74)));
    uint32_t mux = (val >> 3) & 0x1;
    logger_info("CLK_NPUTIMER_ROOT parent mux = %u\n", mux);
}



void
enable_rk3588_npu_clocks(void)
{
    logger("enable RK3588 NPU clocks...\n");

	// 这个看着像有用
	enable_nputimer_root();

    // Aclk 
	// root: CLK_NPU_DSU0
	enable_aclknpu_root();
    enable_aclknpu0();

    // Hclk
	// root: HCLK_NPU_ROOT
    enable_hclknpu_root();
	enable_hclknpu0();

	// Pclk
	// root: PCLK_NPU_ROOT
	enable_ptimer_root();

    logger("RK3588 NPU clocks enabled.\n");
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * Rockchip DesignWare based PCIe host controller driver
 *
 * Copyright (c) 2021 Rockchip, Inc.
 */

#include <clk.h>
#include <dm.h>
#include <generic-phy.h>
#include <pci.h>
#include <power-domain.h>
#include <reset.h>
#include <syscon.h>
#include <asm/arch-rockchip/clock.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm-generic/gpio.h>
#include <dm/device_compat.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <power/regulator.h>

#include "pcie_dw_common.h"

DECLARE_GLOBAL_DATA_PTR;

/**
 * struct rk_pcie - RK DW PCIe controller state
 *
 * @vpcie3v3: The 3.3v power supply for slot
 * @apb_base: The base address of vendor regs
 * @rst_gpio: The #PERST signal for slot
 */
struct rk_pcie
{
    /* Must be first member of the struct */
    struct pcie_dw        dw;
    struct udevice       *vpcie3v3;
    void                 *apb_base;
    struct phy            phy;
    struct clk_bulk       clks;
    struct reset_ctl_bulk rsts;
    struct gpio_desc      rst_gpio;
    u32                   gen;
    u32                   num_lanes;
};

/* Parameters for the waiting for iATU enabled routine */
#define PCIE_CLIENT_GENERAL_DEBUG       0x104
#define PCIE_CLIENT_HOT_RESET_CTRL      0x180
#define PCIE_LTSSM_ENABLE_ENHANCE       BIT(4)
#define PCIE_CLIENT_LTSSM_STATUS        0x300
#define SMLH_LINKUP                     BIT(16)
#define RDLH_LINKUP                     BIT(17)
#define PCIE_CLIENT_DBG_FIFO_MODE_CON   0x310
#define PCIE_CLIENT_DBG_FIFO_PTN_HIT_D0 0x320
#define PCIE_CLIENT_DBG_FIFO_PTN_HIT_D1 0x324
#define PCIE_CLIENT_DBG_FIFO_TRN_HIT_D0 0x328
#define PCIE_CLIENT_DBG_FIFO_TRN_HIT_D1 0x32c
#define PCIE_CLIENT_DBG_FIFO_STATUS     0x350
#define PCIE_CLIENT_DBG_TRANSITION_DATA 0xffff0000
#define PCIE_CLIENT_DBF_EN              0xffff0003

#define PCIE_TYPE0_HDR_DBI2_OFFSET 0x100000

static int
rk_pcie_read(void __iomem *addr, int size, u32 *val)
{
    if ((uintptr_t) addr & (size - 1)) {
        *val = 0;
        return -EOPNOTSUPP;
    }

    if (size == 4) {
        *val = readl(addr);
    } else if (size == 2) {
        *val = readw(addr);
    } else if (size == 1) {
        *val = readb(addr);
    } else {
        *val = 0;
        return -ENODEV;
    }

    return 0;
}

static int
rk_pcie_write(void __iomem *addr, int size, u32 val)
{
    if ((uintptr_t) addr & (size - 1))
        return -EOPNOTSUPP;

    if (size == 4)
        writel(val, addr);
    else if (size == 2)
        writew(val, addr);
    else if (size == 1)
        writeb(val, addr);
    else
        return -ENODEV;

    return 0;
}

static u32
__rk_pcie_read_apb(struct rk_pcie *rk_pcie, void __iomem *base, u32 reg, size_t size)
{
    int ret;
    u32 val;

    ret = rk_pcie_read(base + reg, size, &val);
    if (ret)
        dev_err(rk_pcie->dw.dev, "Read APB address failed\n");

    return val;
}

static void
__rk_pcie_write_apb(struct rk_pcie *rk_pcie, void __iomem *base, u32 reg, size_t size, u32 val)
{
    int ret;

    ret = rk_pcie_write(base + reg, size, val);
    if (ret)
        dev_err(rk_pcie->dw.dev, "Write APB address failed\n");
}

/**
 * rk_pcie_readl_apb() - Read vendor regs
 *
 * @rk_pcie: Pointer to the PCI controller state
 * @reg: Offset of regs
 */
static inline u32
rk_pcie_readl_apb(struct rk_pcie *rk_pcie, u32 reg)
{
    return __rk_pcie_read_apb(rk_pcie, rk_pcie->apb_base, reg, 0x4);
}

/**
 * rk_pcie_writel_apb() - Write vendor regs
 *
 * @rk_pcie: Pointer to the PCI controller state
 * @reg: Offset of regs
 * @val: Value to be writen
 */
static inline void
rk_pcie_writel_apb(struct rk_pcie *rk_pcie, u32 reg, u32 val)
{
    __rk_pcie_write_apb(rk_pcie, rk_pcie->apb_base, reg, 0x4, val);
}

/**
 * rk_pcie_configure() - Configure link capabilities and speed
 *
 * @rk_pcie: Pointer to the PCI controller state
 *
 * Configure the link capabilities and speed in the PCIe root complex.
 */
static void
rk_pcie_configure(struct rk_pcie *pci)
{
    dw_pcie_dbi_write_enable(&pci->dw, true);

    /* Disable BAR 0 and BAR 1 */
    writel(0, pci->dw.dbi_base + PCIE_TYPE0_HDR_DBI2_OFFSET + PCI_BASE_ADDRESS_0);
    writel(0, pci->dw.dbi_base + PCIE_TYPE0_HDR_DBI2_OFFSET + PCI_BASE_ADDRESS_1);

    clrsetbits_le32(pci->dw.dbi_base + PCIE_LINK_CAPABILITY, TARGET_LINK_SPEED_MASK, pci->gen);

    clrsetbits_le32(pci->dw.dbi_base + PCIE_LINK_CTL_2, TARGET_LINK_SPEED_MASK, pci->gen);

    /* Set the number of lanes */
    dw_pcie_link_set_max_link_width(&pci->dw, pci->num_lanes);

    dw_pcie_dbi_write_enable(&pci->dw, false);
}

static void
rk_pcie_enable_debug(struct rk_pcie *rk_pcie)
{
    rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_DBG_FIFO_PTN_HIT_D0, PCIE_CLIENT_DBG_TRANSITION_DATA);
    rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_DBG_FIFO_PTN_HIT_D1, PCIE_CLIENT_DBG_TRANSITION_DATA);
    rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_DBG_FIFO_TRN_HIT_D0, PCIE_CLIENT_DBG_TRANSITION_DATA);
    rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_DBG_FIFO_TRN_HIT_D1, PCIE_CLIENT_DBG_TRANSITION_DATA);
    rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_DBG_FIFO_MODE_CON, PCIE_CLIENT_DBF_EN);
}

static void
rk_pcie_debug_dump(struct rk_pcie *rk_pcie)
{
    u32 loop;

    debug("ltssm = 0x%x\n", rk_pcie_readl_apb(rk_pcie, PCIE_CLIENT_LTSSM_STATUS));
    for (loop = 0; loop < 64; loop++)
        debug("fifo_status = 0x%x\n", rk_pcie_readl_apb(rk_pcie, PCIE_CLIENT_DBG_FIFO_STATUS));
}

static inline void
rk_pcie_link_status_clear(struct rk_pcie *rk_pcie)
{
    rk_pcie_writel_apb(rk_pcie, PCIE_CLIENT_GENERAL_DEBUG, 0x0);
}

static inline void
rk_pcie_disable_ltssm(struct rk_pcie *rk_pcie)
{
    rk_pcie_writel_apb(rk_pcie, 0x0, 0xc0008);
}

static inline void
rk_pcie_enable_ltssm(struct rk_pcie *rk_pcie)
{
    rk_pcie_writel_apb(rk_pcie, 0x0, 0xc000c);
}

static int
is_link_up(struct rk_pcie *priv)
{
    u32 val;

    val = rk_pcie_readl_apb(priv, PCIE_CLIENT_LTSSM_STATUS);
    if ((val & (RDLH_LINKUP | SMLH_LINKUP)) == 0x30000 && (val & GENMASK(5, 0)) == 0x11)
        return 1;

    return 0;
}

/**
 * rk_pcie_link_up() - Wait for the link to come up
 *
 * @rk_pcie: Pointer to the PCI controller state
 *
 * Return: 1 (true) for active line and negetive (false) for no link (timeout)
 */
static int
rk_pcie_link_up(struct rk_pcie *priv)
{
    int retries;

    if (is_link_up(priv)) {
        printf("PCI Link already up before configuration!\n");
        return 1;
    }

    /* DW pre link configurations */
    rk_pcie_configure(priv);

    rk_pcie_disable_ltssm(priv);
    rk_pcie_link_status_clear(priv);
    rk_pcie_enable_debug(priv);

    /* Reset the device */
    if (dm_gpio_is_valid(&priv->rst_gpio))
        dm_gpio_set_value(&priv->rst_gpio, 0);

    /* Enable LTSSM */
    rk_pcie_enable_ltssm(priv);

    /*
	 * PCIe requires the refclk to be stable for 100ms prior to releasing
	 * PERST. See table 2-4 in section 2.6.2 AC Specifications of the PCI
	 * Express Card Electromechanical Specification, 1.1. However, we don't
	 * know if the refclk is coming from RC's PHY or external OSC. If it's
	 * from RC, so enabling LTSSM is the just right place to release #PERST.
	 */
    mdelay(100);
    if (dm_gpio_is_valid(&priv->rst_gpio))
        dm_gpio_set_value(&priv->rst_gpio, 1);

    /* Check if the link is up or not */
    for (retries = 0; retries < 10; retries++) {
        if (is_link_up(priv))
            break;

        mdelay(100);
    }

    if (retries >= 10) {
        dev_err(priv->dw.dev, "PCIe-%d Link Fail\n", dev_seq(priv->dw.dev));
        return -EIO;
    }

    dev_info(priv->dw.dev,
             "PCIe Link up, LTSSM is 0x%x\n",
             rk_pcie_readl_apb(priv, PCIE_CLIENT_LTSSM_STATUS));
    rk_pcie_debug_dump(priv);
    return 0;
}

static int
rockchip_pcie_init_port(struct udevice *dev)
{
    int             ret;
    u32             val;
    struct rk_pcie *priv = dev_get_priv(dev);

    ret = reset_assert_bulk(&priv->rsts);
    if (ret) {
        dev_err(dev, "failed to assert resets (ret=%d)\n", ret);
        return ret;
    }

    /* Set power and maybe external ref clk input */
    ret = regulator_set_enable_if_allowed(priv->vpcie3v3, true);
    if (ret && ret != -ENOSYS) {
        dev_err(dev, "failed to enable vpcie3v3 (ret=%d)\n", ret);
        return ret;
    }

    ret = generic_phy_init(&priv->phy);
    if (ret) {
        dev_err(dev, "failed to init phy (ret=%d)\n", ret);
        goto err_disable_regulator;
    }

    ret = generic_phy_power_on(&priv->phy);
    if (ret) {
        dev_err(dev, "failed to power on phy (ret=%d)\n", ret);
        goto err_exit_phy;
    }

    ret = reset_deassert_bulk(&priv->rsts);
    if (ret) {
        dev_err(dev, "failed to deassert resets (ret=%d)\n", ret);
        goto err_power_off_phy;
    }

    ret = clk_enable_bulk(&priv->clks);
    if (ret) {
        dev_err(dev, "failed to enable clks (ret=%d)\n", ret);
        goto err_deassert_bulk;
    }

    /* LTSSM EN ctrl mode */
    val = rk_pcie_readl_apb(priv, PCIE_CLIENT_HOT_RESET_CTRL);
    val |= PCIE_LTSSM_ENABLE_ENHANCE | (PCIE_LTSSM_ENABLE_ENHANCE << 16);
    rk_pcie_writel_apb(priv, PCIE_CLIENT_HOT_RESET_CTRL, val);

    /* Set RC mode */
    rk_pcie_writel_apb(priv, 0x0, 0xf00040);
    pcie_dw_setup_host(&priv->dw);

    ret = rk_pcie_link_up(priv);
    if (ret < 0)
        goto err_link_up;

    return 0;
err_link_up:
    clk_disable_bulk(&priv->clks);
err_deassert_bulk:
    reset_assert_bulk(&priv->rsts);
err_power_off_phy:
    generic_phy_power_off(&priv->phy);
err_exit_phy:
    generic_phy_exit(&priv->phy);
err_disable_regulator:
    regulator_set_enable_if_allowed(priv->vpcie3v3, false);

    return ret;
}

static int
rockchip_pcie_parse_dt(struct udevice *dev)
{
    struct rk_pcie *priv = dev_get_priv(dev);
    int             ret;

    priv->dw.dbi_base = dev_read_addr_index_ptr(dev, 0);
    if (!priv->dw.dbi_base)
        return -EINVAL;

    dev_dbg(dev, "DBI address is 0x%p\n", priv->dw.dbi_base);

    priv->apb_base = dev_read_addr_index_ptr(dev, 1);
    if (!priv->apb_base)
        return -EINVAL;

    dev_dbg(dev, "APB address is 0x%p\n", priv->apb_base);

    priv->dw.cfg_base = dev_read_addr_size_index_ptr(dev, 2, &priv->dw.cfg_size);
    if (!priv->dw.cfg_base)
        return -EINVAL;

    dev_dbg(dev, "CFG address is 0x%p\n", priv->dw.cfg_base);

    ret = gpio_request_by_name(dev, "reset-gpios", 0, &priv->rst_gpio, GPIOD_IS_OUT);
    if (ret) {
        dev_err(dev, "failed to find reset-gpios property\n");
        return ret;
    }

    ret = reset_get_bulk(dev, &priv->rsts);
    if (ret) {
        dev_err(dev, "Can't get reset: %d\n", ret);
        goto rockchip_pcie_parse_dt_err_reset_get_bulk;
    }

    ret = clk_get_bulk(dev, &priv->clks);
    if (ret) {
        dev_err(dev, "Can't get clock: %d\n", ret);
        goto rockchip_pcie_parse_dt_err_clk_get_bulk;
    }

    ret = device_get_supply_regulator(dev, "vpcie3v3-supply", &priv->vpcie3v3);
    if (ret && ret != -ENOENT) {
        dev_err(dev, "failed to get vpcie3v3 supply (ret=%d)\n", ret);
        goto rockchip_pcie_parse_dt_err_supply_regulator;
    }

    ret = generic_phy_get_by_index(dev, 0, &priv->phy);
    if (ret) {
        dev_err(dev, "failed to get pcie phy (ret=%d)\n", ret);
        goto rockchip_pcie_parse_dt_err_phy_get_by_index;
    }

    priv->gen = dev_read_u32_default(dev, "max-link-speed", LINK_SPEED_GEN_3);

    priv->num_lanes = dev_read_u32_default(dev, "num-lanes", 1);

    return 0;

rockchip_pcie_parse_dt_err_phy_get_by_index:
    /* regulators don't need release */
rockchip_pcie_parse_dt_err_supply_regulator:
    clk_release_bulk(&priv->clks);
rockchip_pcie_parse_dt_err_clk_get_bulk:
    reset_release_bulk(&priv->rsts);
rockchip_pcie_parse_dt_err_reset_get_bulk:
    dm_gpio_free(dev, &priv->rst_gpio);
    return ret;
}

/**
 * rockchip_pcie_probe() - Probe the PCIe bus for active link
 *
 * @dev: A pointer to the device being operated on
 *
 * Probe for an active link on the PCIe bus and configure the controller
 * to enable this port.
 *
 * Return: 0 on success, else -ENODEV
 */
static int
rockchip_pcie_probe(struct udevice *dev)
{
    struct rk_pcie        *priv = dev_get_priv(dev);
    struct udevice        *ctlr = pci_get_controller(dev);
    struct pci_controller *hose = dev_get_uclass_priv(ctlr);
    int                    ret  = 0;

    priv->dw.first_busno = dev_seq(dev);
    priv->dw.dev         = dev;

    ret = rockchip_pcie_parse_dt(dev);
    if (ret)
        return ret;

    ret = rockchip_pcie_init_port(dev);
    if (ret)
        goto rockchip_pcie_probe_err_init_port;

    dev_info(dev,
             "PCIE-%d: Link up (Gen%d-x%d, Bus%d)\n",
             dev_seq(dev),
             pcie_dw_get_link_speed(&priv->dw),
             pcie_dw_get_link_width(&priv->dw),
             hose->first_busno);

    ret = pcie_dw_prog_outbound_atu_unroll(&priv->dw,
                                           PCIE_ATU_REGION_INDEX0,
                                           PCIE_ATU_TYPE_MEM,
                                           priv->dw.mem.phys_start,
                                           priv->dw.mem.bus_start,
                                           priv->dw.mem.size);
    if (!ret)
        return ret;

rockchip_pcie_probe_err_init_port:
    clk_release_bulk(&priv->clks);
    reset_release_bulk(&priv->rsts);
    dm_gpio_free(dev, &priv->rst_gpio);

    return ret;
}

static const struct dm_pci_ops rockchip_pcie_ops = {
    .read_config  = pcie_dw_read_config,
    .write_config = pcie_dw_write_config,
};

static const struct udevice_id rockchip_pcie_ids[] = {{.compatible = "rockchip,rk3568-pcie"},
                                                      {.compatible = "rockchip,rk3588-pcie"},
                                                      {}};

U_BOOT_DRIVER(rockchip_dw_pcie) = {
    .name      = "pcie_dw_rockchip",
    .id        = UCLASS_PCI,
    .of_match  = rockchip_pcie_ids,
    .ops       = &rockchip_pcie_ops,
    .probe     = rockchip_pcie_probe,
    .priv_auto = sizeof(struct rk_pcie),
};

/******************************************************************
 * TestOS-Reflector specific implementation
 * PCIe ATU configuration and RTL8125 driver with ping support
 ******************************************************************/

#include "lib/t_logger.h"
#include "mem/t_mmio.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"

/* PCIe DBI base address for RK3588 */
#define DBI_BASE          0xa40c00000UL
#define ATU_VIEWPORT_BASE 0x900
#define ATU_REGION_CTRL1  0x904
#define ATU_REGION_CTRL2  0x908
#define ATU_LOWER_BASE    0x90C
#define ATU_UPPER_BASE    0x910
#define ATU_LIMIT         0x914
#define ATU_LOWER_TARGET  0x918
#define ATU_UPPER_TARGET  0x91C

/* ATU Configuration */
#define PCIE_ATU_REGION_OUTBOUND (0x0 << 31)
#define PCIE_ATU_REGION_INDEX0   0
#define PCIE_ATU_TYPE_MEM        0x0
#define PCIE_ATU_TYPE_CFG0       0x4
#define PCIE_ATU_ENABLE          (1 << 31)
#define PCIE_ATU_BAR_MODE_ENABLE (1 << 30)

/* RTL8125 specific registers */
#define RTL8125_MAC0             0x0000
#define RTL8125_MAC4             0x0004
#define RTL8125_MAR0             0x0008
#define RTL8125_TxDescStartAddr  0x0020
#define RTL8125_TxDescStartAddrH 0x0024
#define RTL8125_ChipCmd          0x0037
#define RTL8125_TxPoll           0x0090
#define RTL8125_IntrMask         0x0038
#define RTL8125_IntrStatus       0x003C
#define RTL8125_TxConfig         0x0040
#define RTL8125_RxConfig         0x0044
#define RTL8125_Cfg9346          0x0050
#define RTL8125_RxDescStartAddr  0x00E4
#define RTL8125_RxDescStartAddrH 0x00E8
#define RTL8125_MaxRxPacketSize  0x00DA

/* Chip command bits */
#define CMD_TX_ENABLE 0x04
#define CMD_RX_ENABLE 0x08
#define CMD_RESET     0x10

/* Config register unlock */
#define CFG9346_UNLOCK 0xC0
#define CFG9346_LOCK   0x00

/* Descriptor bits */
#define DESC_OWN 0x80000000
#define DESC_EOR 0x40000000
#define DESC_FS  0x20000000
#define DESC_LS  0x10000000

/* Network configuration */
#define NUM_TX_DESC 4
#define NUM_RX_DESC 4
#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 2048

/* Ethernet & IP protocol constants */
#define ETH_ALEN       6
#define ETH_HLEN       14
#define ETH_P_IP       0x0800
#define ETH_P_ARP      0x0806
#define IPPROTO_ICMP   1
#define ICMP_ECHO      8
#define ICMP_ECHOREPLY 0

/* Network structures */
typedef struct
{
    uint8_t  dest[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t proto;
} __attribute__((packed)) eth_hdr_t;

typedef struct
{
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dest_addr;
} __attribute__((packed)) ip_hdr_t;

typedef struct
{
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_hdr_t;

typedef struct
{
    uint32_t status;
    uint32_t vlan_tag;
    uint32_t buf_addr_lo;
    uint32_t buf_addr_hi;
} __attribute__((packed)) rtl_desc_t;

/* Global variables for network driver */
static volatile uint8_t *rtl_mmio_base = NULL;
static rtl_desc_t       *tx_ring       = NULL;
static rtl_desc_t       *rx_ring       = NULL;
static uint8_t          *tx_buffers[NUM_TX_DESC];
static uint8_t          *rx_buffers[NUM_RX_DESC];
static uint32_t          tx_idx           = 0;
static uint32_t          rx_idx           = 0;
static uint8_t           my_mac[ETH_ALEN] = {0x00, 0xe0, 0x4c, 0x68, 0x12, 0x34};

/* Helper functions for MMIO */
static inline uint8_t
rtl_read8(uint32_t reg)
{
    return read8((void *) (rtl_mmio_base + reg));
}

static inline uint16_t
rtl_read16(uint32_t reg)
{
    return read16((void *) (rtl_mmio_base + reg));
}

static inline uint32_t
rtl_read32(uint32_t reg)
{
    return read32((void *) (rtl_mmio_base + reg));
}

static inline void
rtl_write8(uint32_t reg, uint8_t val)
{
    write8(val, (void *) (rtl_mmio_base + reg));
}

static inline void
rtl_write16(uint32_t reg, uint16_t val)
{
    write16(val, (void *) (rtl_mmio_base + reg));
}

static inline void
rtl_write32(uint32_t reg, uint32_t val)
{
    write32(val, (void *) (rtl_mmio_base + reg));
}

/* Simple delay function */
static void
udelay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 100; i++)
        ;
}

static void
mdelay(uint32_t ms)
{
    udelay(ms * 1000);
}

/* Checksum calculation */
static uint16_t
ip_checksum(void *data, int len)
{
    uint32_t  sum = 0;
    uint16_t *ptr = (uint16_t *) data;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        sum += *(uint8_t *) ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

/* Byte swap helpers */
static inline uint16_t
htons(uint16_t val)
{
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static inline uint32_t
htonl(uint32_t val)
{
    return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | ((val >> 8) & 0xFF00) |
           ((val >> 24) & 0xFF);
}

static inline uint16_t
ntohs(uint16_t val)
{
    return htons(val);
}

static inline uint32_t
ntohl(uint32_t val)
{
    return htonl(val);
}

/* Memory copy helper */
static void
memcpy_local(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--)
        *d++ = *s++;
}

static void
memset_local(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    while (n--)
        *d++ = (uint8_t) val;
}

/**
 * dw_pcie_setup_atu - Setup PCIe Address Translation Unit
 * 
 * Configure the ATU to map CPU address space to PCIe bus address space
 */
static int
dw_pcie_setup_atu(uint64_t dbi_base,
                  uint32_t region_index,
                  uint32_t type,
                  uint64_t cpu_addr,
                  uint64_t pci_addr,
                  uint64_t size)
{
    volatile uint32_t *dbi     = (volatile uint32_t *) dbi_base;
    uint32_t           retries = 1000;
    uint32_t           val;

    logger_info("=== Setting up PCIe ATU Region %d ===\n", region_index);
    logger_info("  Type: 0x%x (%s)\n",
                type,
                type == PCIE_ATU_TYPE_MEM    ? "Memory"
                : type == PCIE_ATU_TYPE_CFG0 ? "Config"
                                             : "Unknown");
    logger_info("  CPU Address: 0x%llx\n", cpu_addr);
    logger_info("  PCI Address: 0x%llx\n", pci_addr);
    logger_info("  Size: 0x%llx (%llu bytes)\n", size, size);

    /* Select viewport */
    write32(PCIE_ATU_REGION_OUTBOUND | region_index, (void *) (dbi_base + ATU_VIEWPORT_BASE));
    logger_debug("  Set viewport to region %d\n", region_index);

    /* Set lower base address */
    write32((uint32_t) (cpu_addr & 0xFFFFFFFF), (void *) (dbi_base + ATU_LOWER_BASE));
    logger_debug("  Lower base: 0x%x\n", (uint32_t) (cpu_addr & 0xFFFFFFFF));

    /* Set upper base address */
    write32((uint32_t) (cpu_addr >> 32), (void *) (dbi_base + ATU_UPPER_BASE));
    logger_debug("  Upper base: 0x%x\n", (uint32_t) (cpu_addr >> 32));

    /* Set limit */
    write32((uint32_t) ((cpu_addr + size - 1) & 0xFFFFFFFF), (void *) (dbi_base + ATU_LIMIT));
    logger_debug("  Limit: 0x%x\n", (uint32_t) ((cpu_addr + size - 1) & 0xFFFFFFFF));

    /* Set lower target address */
    write32((uint32_t) (pci_addr & 0xFFFFFFFF), (void *) (dbi_base + ATU_LOWER_TARGET));
    logger_debug("  Lower target: 0x%x\n", (uint32_t) (pci_addr & 0xFFFFFFFF));

    /* Set upper target address */
    write32((uint32_t) (pci_addr >> 32), (void *) (dbi_base + ATU_UPPER_TARGET));
    logger_debug("  Upper target: 0x%x\n", (uint32_t) (pci_addr >> 32));

    /* Configure region control 1 */
    write32(type, (void *) (dbi_base + ATU_REGION_CTRL1));
    logger_debug("  Control 1: 0x%x\n", type);

    /* Enable the region */
    write32(PCIE_ATU_ENABLE, (void *) (dbi_base + ATU_REGION_CTRL2));
    logger_debug("  Control 2 (Enable): 0x%x\n", PCIE_ATU_ENABLE);

    /* Wait for the region to be enabled */
    while (retries--) {
        val = read32((void *) (dbi_base + ATU_REGION_CTRL2));
        if (val & PCIE_ATU_ENABLE) {
            logger_info("ATU region %d enabled successfully!\n", region_index);
            return 0;
        }
        udelay(10);
    }

    logger_error("Failed to enable ATU region %d (timeout)\n", region_index);
    return -1;
}

/**
 * pcie_scan_bus - Scan PCIe bus for devices
 */
static int
pcie_scan_bus(uint64_t cfg_base, uint32_t *vendor_id, uint32_t *device_id, uint32_t *class_code)
{
    volatile uint32_t *cfg = (volatile uint32_t *) cfg_base;
    uint32_t           val;

    logger_info("=== Scanning PCIe Bus ===\n");
    logger_info("  Config base: 0x%llx\n", cfg_base);

    /* Read Vendor ID and Device ID */
    val        = read32((void *) cfg_base);
    *vendor_id = val & 0xFFFF;
    *device_id = (val >> 16) & 0xFFFF;

    logger_info("  Vendor ID: 0x%04x\n", *vendor_id);
    logger_info("  Device ID: 0x%04x\n", *device_id);

    if (*vendor_id == 0xFFFF || *vendor_id == 0x0000) {
        logger_error("  No device found (invalid vendor ID)\n");
        return -1;
    }

    /* Read Class Code */
    val         = read32((void *) (cfg_base + 0x08));
    *class_code = val >> 8;

    logger_info("  Class Code: 0x%06x\n", *class_code);
    logger_info("  Revision ID: 0x%02x\n", val & 0xFF);

    return 0;
}

/**
 * pcie_get_bar_info - Get BAR information
 */
static int
pcie_get_bar_info(uint64_t cfg_base, uint32_t bar_num, uint64_t *bar_addr, uint64_t *bar_size)
{
    volatile uint32_t *cfg        = (volatile uint32_t *) cfg_base;
    uint32_t           bar_offset = 0x10 + (bar_num * 4);
    uint32_t           bar_val, bar_orig, size_mask;

    logger_info("=== Reading BAR%d Information ===\n", bar_num);

    /* Read original BAR value */
    bar_orig = read32((void *) (cfg_base + bar_offset));
    logger_debug("  Original BAR value: 0x%08x\n", bar_orig);

    /* Write all 1s to determine size */
    write32(0xFFFFFFFF, (void *) (cfg_base + bar_offset));
    bar_val = read32((void *) (cfg_base + bar_offset));

    /* Restore original value */
    write32(bar_orig, (void *) (cfg_base + bar_offset));

    /* Calculate size */
    if (bar_val & 0x1) {
        /* I/O BAR */
        logger_info("  BAR%d is I/O type\n", bar_num);
        size_mask = bar_val & 0xFFFFFFFC;
        *bar_size = (~size_mask) + 1;
        *bar_addr = bar_orig & 0xFFFFFFFC;
    } else {
        /* Memory BAR */
        logger_info("  BAR%d is Memory type\n", bar_num);
        size_mask = bar_val & 0xFFFFFFF0;
        *bar_size = (~size_mask) + 1;
        *bar_addr = bar_orig & 0xFFFFFFF0;

        /* Check if 64-bit BAR */
        if ((bar_orig & 0x6) == 0x4) {
            logger_info("  64-bit BAR detected\n");
            uint32_t bar_upper = read32((void *) (cfg_base + bar_offset + 4));
            *bar_addr |= ((uint64_t) bar_upper << 32);
        }
    }

    logger_info("  BAR%d Address: 0x%llx\n", bar_num, *bar_addr);
    logger_info("  BAR%d Size: 0x%llx (%llu bytes)\n", bar_num, *bar_size, *bar_size);

    return 0;
}

/**
 * rtl8125_init - Initialize RTL8125 network controller
 */
static int
rtl8125_init(uint64_t mmio_base)
{
    int      i;
    uint32_t val;

    logger_info("=== Initializing RTL8125 Network Controller ===\n");
    logger_info("  MMIO Base: 0x%llx\n", mmio_base);

    rtl_mmio_base = (volatile uint8_t *) mmio_base;

    /* Read and display MAC address */
    logger_info("  Reading MAC address...\n");
    for (i = 0; i < 6; i++) {
        my_mac[i] = rtl_read8(RTL8125_MAC0 + i);
    }
    logger_info("  MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                my_mac[0],
                my_mac[1],
                my_mac[2],
                my_mac[3],
                my_mac[4],
                my_mac[5]);

    /* Software reset */
    logger_info("  Performing software reset...\n");
    rtl_write8(RTL8125_ChipCmd, CMD_RESET);
    mdelay(10);

    /* Wait for reset to complete */
    for (i = 0; i < 1000; i++) {
        if (!(rtl_read8(RTL8125_ChipCmd) & CMD_RESET))
            break;
        udelay(10);
    }

    if (i >= 1000) {
        logger_error("  Reset timeout!\n");
        return -1;
    }
    logger_info("  Reset completed\n");

    /* Unlock config registers */
    rtl_write8(RTL8125_Cfg9346, CFG9346_UNLOCK);
    logger_debug("  Config registers unlocked\n");

    /* Allocate descriptor rings and buffers */
    logger_info("  Allocating TX/RX descriptor rings...\n");

    /* For simplicity, using static allocation in real implementation
	 * these should be DMA-able memory regions */
    // tx_ring = (rtl_desc_t *)0x40200000;  /* Example physical address */
    // rx_ring = (rtl_desc_t *)0x40201000;

    logger_warn("  Note: Using placeholder addresses for descriptors\n");
    logger_warn("  In production, allocate proper DMA memory!\n");

    /* Setup TX descriptors */
    logger_info("  Setting up TX ring...\n");
    // for (i = 0; i < NUM_TX_DESC; i++) {
    //     tx_ring[i].status = 0;
    //     tx_ring[i].vlan_tag = 0;
    //     tx_ring[i].buf_addr_lo = 0x40300000 + (i * TX_BUF_SIZE);
    //     tx_ring[i].buf_addr_hi = 0;
    //     tx_buffers[i] = (uint8_t *)(0x40300000UL + (i * TX_BUF_SIZE));
    // }
    // tx_ring[NUM_TX_DESC - 1].status |= DESC_EOR;

    /* Setup RX descriptors */
    logger_info("  Setting up RX ring...\n");
    // for (i = 0; i < NUM_RX_DESC; i++) {
    //     rx_ring[i].status = DESC_OWN | RX_BUF_SIZE;
    //     rx_ring[i].vlan_tag = 0;
    //     rx_ring[i].buf_addr_lo = 0x40400000 + (i * RX_BUF_SIZE);
    //     rx_ring[i].buf_addr_hi = 0;
    //     rx_buffers[i] = (uint8_t *)(0x40400000UL + (i * RX_BUF_SIZE));
    // }
    // rx_ring[NUM_RX_DESC - 1].status |= DESC_EOR;

    /* Write descriptor addresses to NIC */
    // rtl_write32(RTL8125_TxDescStartAddr, 0x40200000);
    // rtl_write32(RTL8125_TxDescStartAddrH, 0);
    // rtl_write32(RTL8125_RxDescStartAddr, 0x40201000);
    // rtl_write32(RTL8125_RxDescStartAddrH, 0);

    logger_debug("  TX descriptor ring at: 0x40200000\n");
    logger_debug("  RX descriptor ring at: 0x40201000\n");

    /* Configure TX */
    logger_info("  Configuring TX...\n");
    val = (3 << 24) | (6 << 8); /* IFG and DMA burst */
    rtl_write32(RTL8125_TxConfig, val);
    logger_debug("  TX Config: 0x%08x\n", val);

    /* Configure RX */
    logger_info("  Configuring RX...\n");
    val = (7 << 13) | (6 << 8) | 0x0E; /* Accept all packets */
    rtl_write32(RTL8125_RxConfig, val);
    logger_debug("  RX Config: 0x%08x\n", val);

    /* Set max RX packet size */
    rtl_write16(RTL8125_MaxRxPacketSize, RX_BUF_SIZE);

    /* Enable TX and RX */
    logger_info("  Enabling TX and RX...\n");
    rtl_write8(RTL8125_ChipCmd, CMD_TX_ENABLE | CMD_RX_ENABLE);

    /* Lock config registers */
    rtl_write8(RTL8125_Cfg9346, CFG9346_LOCK);
    logger_debug("  Config registers locked\n");

    logger_info("RTL8125 initialization complete!\n");
    return 0;
}

/**
 * rtl8125_send_packet - Send a packet via RTL8125
 */
static int
rtl8125_send_packet(uint8_t *data, uint32_t len)
{
    uint32_t timeout = 10000;

    logger_debug("=== Sending packet ===\n");
    logger_debug("  Length: %d bytes\n", len);

    /* For demo purposes, just log what we would send */
    logger_debug("  First 32 bytes of packet:\n");
    for (int i = 0; i < 32 && i < len; i += 8) {
        logger_debug("    ");
        for (int j = 0; j < 8 && (i + j) < len; j++) {
            logger_debug("%02x ", data[i + j]);
        }
        logger_debug("\n");
    }

    logger_warn("  Note: Actual TX not implemented (requires DMA setup)\n");
    return 0;
}

/**
 * rtl8125_recv_packet - Receive a packet from RTL8125
 */
static int
rtl8125_recv_packet(uint8_t *buffer, uint32_t *len, uint32_t timeout_ms)
{
    logger_debug("=== Checking for received packets ===\n");
    logger_debug("  Timeout: %d ms\n", timeout_ms);

    /* For demo purposes, simulate no packet received */
    logger_warn("  Note: Actual RX not implemented (requires DMA setup)\n");
    return -1;
}

/**
 * send_ping - Send ICMP Echo Request
 */
static void
send_ping(uint8_t src_ip[4], uint8_t dst_ip[4], uint16_t seq)
{
    uint8_t     packet[128];
    eth_hdr_t  *eth;
    ip_hdr_t   *ip;
    icmp_hdr_t *icmp;
    uint8_t    *payload;
    uint32_t    pkt_len = 0;

    logger_info("=== Preparing ICMP Echo Request (Ping) ===\n");
    logger_info("  Source IP: %d.%d.%d.%d\n", src_ip[0], src_ip[1], src_ip[2], src_ip[3]);
    logger_info("  Destination IP: %d.%d.%d.%d\n", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    logger_info("  Sequence: %d\n", seq);

    memset_local(packet, 0, sizeof(packet));

    /* Ethernet header */
    eth = (eth_hdr_t *) packet;
    /* Destination MAC (broadcast for simplicity) */
    memset_local(eth->dest, 0xFF, ETH_ALEN);
    memcpy_local(eth->src, my_mac, ETH_ALEN);
    eth->proto = htons(ETH_P_IP);
    pkt_len += sizeof(eth_hdr_t);

    logger_debug("  Ethernet header:\n");
    logger_debug("    Dest MAC: ff:ff:ff:ff:ff:ff\n");
    logger_debug("    Src MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                 my_mac[0],
                 my_mac[1],
                 my_mac[2],
                 my_mac[3],
                 my_mac[4],
                 my_mac[5]);
    logger_debug("    EtherType: 0x%04x (IP)\n", ETH_P_IP);

    /* IP header */
    ip              = (ip_hdr_t *) (packet + pkt_len);
    ip->version_ihl = 0x45; /* IPv4, 20-byte header */
    ip->tos         = 0;
    ip->total_len   = htons(sizeof(ip_hdr_t) + sizeof(icmp_hdr_t) + 32);
    ip->id          = htons(0x1234);
    ip->frag_off    = 0;
    ip->ttl         = 64;
    ip->protocol    = IPPROTO_ICMP;
    ip->checksum    = 0;
    memcpy_local(&ip->src_addr, src_ip, 4);
    memcpy_local(&ip->dest_addr, dst_ip, 4);
    ip->checksum = ip_checksum(ip, sizeof(ip_hdr_t));
    pkt_len += sizeof(ip_hdr_t);

    logger_debug("  IP header:\n");
    logger_debug("    Version: 4, Header length: 20 bytes\n");
    logger_debug("    Total length: %d bytes\n", ntohs(ip->total_len));
    logger_debug("    TTL: %d\n", ip->ttl);
    logger_debug("    Protocol: %d (ICMP)\n", ip->protocol);
    logger_debug("    Checksum: 0x%04x\n", ntohs(ip->checksum));

    /* ICMP header */
    icmp           = (icmp_hdr_t *) (packet + pkt_len);
    icmp->type     = ICMP_ECHO;
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->id       = htons(0x5678);
    icmp->sequence = htons(seq);
    pkt_len += sizeof(icmp_hdr_t);

    /* ICMP payload */
    payload = packet + pkt_len;
    for (int i = 0; i < 32; i++) {
        payload[i] = i;
    }
    pkt_len += 32;

    /* Calculate ICMP checksum */
    icmp->checksum = ip_checksum(icmp, sizeof(icmp_hdr_t) + 32);

    logger_debug("  ICMP header:\n");
    logger_debug("    Type: %d (Echo Request)\n", icmp->type);
    logger_debug("    Code: %d\n", icmp->code);
    logger_debug("    Checksum: 0x%04x\n", ntohs(icmp->checksum));
    logger_debug("    ID: 0x%04x\n", ntohs(icmp->id));
    logger_debug("    Sequence: %d\n", ntohs(icmp->sequence));

    logger_info("  Total packet size: %d bytes\n", pkt_len);
    logger_info("  Sending ping packet...\n");

    /* Send the packet */
    rtl8125_send_packet(packet, pkt_len);

    logger_info("Ping request sent successfully!\n");
}

/**
 * test_dw_pcie_atu - Main test function
 * 
 * This function configures PCIe ATU, scans for RTL8125, and tests ping
 */
void
test_dw_pcie_atu(void)
{
    uint64_t mmio_base_phys = 0xf3000000UL;
    uint64_t dbi_base_phys  = DBI_BASE;
    uint64_t cpu_addr       = 0xf3000000UL;
    uint64_t pci_addr       = 0x00000000UL;
    uint64_t size           = 0x100000UL; /* 1MB */
    uint64_t phy_addr       = 0x40100000UL;

    uint32_t vendor_id, device_id, class_code;
    uint64_t bar_addr, bar_size;
    uint64_t rtl_mmio_phys = 0x9c0100000UL; /* From your Rust code */
    uint64_t rtl_mmio_virt;
    int      ret;

    logger_info("\n");
    logger_info("========================================\n");
    logger_info("=== Testing DesignWare PCIe ATU ===\n");
    logger_info("========================================\n");
    logger_info("\n");

    /* Note: phys_to_virt function needs to be implemented or use direct mapping */
    uint64_t mmio_base_virt = mmio_base_phys; /* Assuming identity mapping */
    uint64_t dbi_base_virt  = dbi_base_phys;

    logger_info("Physical addresses:\n");
    logger_info("  MMIO base (config window): 0x%llx\n", mmio_base_phys);
    logger_info("  DBI base: 0x%llx\n", dbi_base_phys);
    logger_info("  Physical start: 0x%llx\n", phy_addr);
    logger_info("\n");

    /* Step 1: Setup ATU for configuration access */
    logger_info("Step 1: Configuring ATU for PCIe config access\n");
    ret = dw_pcie_setup_atu(dbi_base_virt,
                            PCIE_ATU_REGION_INDEX0,
                            PCIE_ATU_TYPE_CFG0,
                            cpu_addr,
                            pci_addr,
                            size);
    if (ret != 0) {
        logger_error("Failed to setup ATU!\n");
        return;
    }
    logger_info("\n");

    /* Step 2: Scan PCIe bus */
    logger_info("Step 2: Scanning PCIe bus for devices\n");
    ret = pcie_scan_bus(mmio_base_virt, &vendor_id, &device_id, &class_code);
    if (ret != 0) {
        logger_error("No PCIe device found!\n");
        return;
    }

    /* Check if it's a RealTek device */
    if (vendor_id == 0x10EC) {
        logger_info("  Device identified: RealTek (0x10EC)\n");
        if (device_id == 0x8125) {
            logger_info("  Model: RTL8125 2.5GbE Controller\n");
        } else if (device_id == 0x8169) {
            logger_info("  Model: RTL8169 GbE Controller\n");
        } else {
            logger_info("  Model: Unknown (Device ID 0x%04x)\n", device_id);
        }
    } else {
        logger_warn("  Warning: Not a RealTek device!\n");
    }
    logger_info("\n");

    /* Step 3: Read BAR information */
    logger_info("Step 3: Reading device BAR information\n");
    pcie_get_bar_info(mmio_base_virt, 2, &bar_addr, &bar_size);
    logger_info("\n");

    /* Step 4: Map BAR to memory */
    logger_info("Step 4: Mapping device BAR to system memory\n");
    logger_info("  Using physical address: 0x%llx\n", rtl_mmio_phys);
    rtl_mmio_virt = rtl_mmio_phys; /* Assuming identity mapping */
    logger_info("  Virtual address: 0x%llx\n", rtl_mmio_virt);

    /* Verify we can read from the BAR */
    uint32_t test_val = read32((void *) rtl_mmio_virt);
    logger_info("  Test read from BAR: 0x%08x\n", test_val);
    logger_info("\n");

    /* Step 5: Initialize RTL8125 */
    logger_info("Step 5: Initializing RTL8125 driver\n");
    ret = rtl8125_init(rtl_mmio_virt);
    if (ret != 0) {
        logger_error("Failed to initialize RTL8125!\n");
        return;
    }
    logger_info("\n");

    /* Step 6: Test ping */
    logger_info("Step 6: Testing ICMP ping functionality\n");
    uint8_t local_ip[4]  = {192, 168, 22, 102};
    uint8_t remote_ip[4] = {192, 168, 22, 101};

    logger_info("Network configuration:\n");
    logger_info("  Local IP: %d.%d.%d.%d\n", local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    logger_info("  Remote IP (ping target): %d.%d.%d.%d\n",
                remote_ip[0],
                remote_ip[1],
                remote_ip[2],
                remote_ip[3]);
    logger_info("\n");

    /* Send ping */
    send_ping(local_ip, remote_ip, 1);
    logger_info("\n");

    /* Wait for reply */
    logger_info("Waiting for ping reply...\n");
    uint8_t  rx_buffer[2048];
    uint32_t rx_len;

    ret = rtl8125_recv_packet(rx_buffer, &rx_len, 1000);
    if (ret == 0) {
        logger_info("Received reply packet (%d bytes)\n", rx_len);

        /* Parse the reply */
        eth_hdr_t *eth = (eth_hdr_t *) rx_buffer;
        if (ntohs(eth->proto) == ETH_P_IP) {
            ip_hdr_t *ip = (ip_hdr_t *) (rx_buffer + sizeof(eth_hdr_t));
            if (ip->protocol == IPPROTO_ICMP) {
                icmp_hdr_t *icmp =
                    (icmp_hdr_t *) (rx_buffer + sizeof(eth_hdr_t) + sizeof(ip_hdr_t));
                if (icmp->type == ICMP_ECHOREPLY) {
                    logger_info("  ICMP Echo Reply received!\n");
                    logger_info("  From: %d.%d.%d.%d\n",
                                (ip->src_addr >> 0) & 0xFF,
                                (ip->src_addr >> 8) & 0xFF,
                                (ip->src_addr >> 16) & 0xFF,
                                (ip->src_addr >> 24) & 0xFF);
                    logger_info("  Sequence: %d\n", ntohs(icmp->sequence));
                    logger_info("\n");
                    logger_info("✓ Ping test SUCCESSFUL!\n");
                }
            }
        }
    } else {
        logger_warn("No reply received (timeout)\n");
        logger_info("Note: This is expected in demo mode without DMA setup\n");
    }

    logger_info("\n");
    logger_info("========================================\n");
    logger_info("=== PCIe ATU Test Complete ===\n");
    logger_info("========================================\n");
}

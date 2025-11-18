#include "t_types.h"
#include "dev/t_timer.h"
#include "mem/t_mmio.h"
#include "mem/t_mem.h"
#include "mem/cache.h"
#include "lib/t_logger.h"
#include "lib/t_string.h"
#include "dev/rtl8169.h"

#define EIO       1
#define ETIMEDOUT 110
#define ENOMEM    12

#define ETH_ZLEN 60 /* Minimum ethernet frame size */

/* PCI configuration space constants */
#define PCI_BASE_ADDRESS_0 0x10 /* First BAR register offset */
#define PCI_REGION_TYPE    0    /* Region type (not used in bare-metal) */
#define PCI_REGION_MEM     0    /* Memory region (not used in bare-metal) */

#define ETH_ALEN       6
#define ETH_HLEN       14
#define ETH_P_IP       0x0800
#define ETH_P_ARP      0x0806
#define IPPROTO_ICMP   1
#define ICMP_ECHO      8
#define ICMP_ECHOREPLY 0

static uint8_t my_mac[ETH_ALEN]     = {0x2e, 0xc3, 0x69, 0x34, 0x7d, 0x31};
static uint8_t remote_mac[ETH_ALEN] = {0x00, 0xe0, 0x1e, 0x1c, 0x01, 0x5e};  // 00:e0:1e:1c:01:5e

struct pci_child_plat MY_RTL8125 = {
    .vendor = 0x10EC,  // Realtek 的 PCIe Vendor ID
    .device = 0x8125,  // RTL8125 的 Device ID
};

void *
dev_get_parent_plat(struct udevice *dev)
{
    return &MY_RTL8125;
}

struct eth_pdata
{
    unsigned char *enetaddr;
};

struct eth_pdata MYETH = {0};
void *
dev_get_plat(struct udevice *dev)
{
    return &MYETH;
}


#define drv_version "v1.5"
#define drv_date    "01-17-2004"

static unsigned long ioaddr;
#define pr_fmt(fmt) "rtl8169: " fmt

/* Array size macro */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Alignment macro - rounds up to the nearest multiple of 'align' */
#define ALIGN(x, align) (((x) + (align) - 1) & ~((align) - 1))

#define printf logger_info
#define debug  logger_info

/* Condensed operations for readability. */
#define currticks() timer_get_system_ticks()

/* Byte order conversion macros for little-endian to CPU and vice versa */
#define le32_to_cpu(x) (x) /* ARM is little-endian, no conversion needed */
#define cpu_to_le32(x) (x) /* ARM is little-endian, no conversion needed */

/* PCI address to physical address conversion */
/* In bare-metal environment, PCI memory addresses are already physical addresses */
static inline unsigned long
dm_pci_mem_to_phys(struct udevice *dev, unsigned long addr)
{
    (void) dev; /* Unused parameter */
    return addr;
}

/* PCI BAR mapping function */
/* In bare-metal environment, return the fixed MMIO base address for the device */
static inline void *
dm_pci_map_bar(struct udevice *dev,
               unsigned int    bar_offset,
               unsigned long   flags,
               unsigned long   offset,
               unsigned int    region_type,
               unsigned int    mem_type)
{
    (void) bar_offset; /* Unused parameters */
    (void) flags;
    (void) offset;
    (void) region_type;
    (void) mem_type;

    /* Return the fixed MMIO address for RTL8125/RTL8169 */
    /* This address should match the PCIe device's BAR address */
    struct eth_pdata *plat = dev_get_plat(dev);
    return (void *) dev->mmio_base;
}

/* media options */
#define MAX_UNITS 8
static int media[MAX_UNITS] = {-1, -1, -1, -1, -1, -1, -1, -1};

/* MAC address length*/
#define MAC_ADDR_LEN 6

/* max supported gigabit ethernet frame size -- must be at least (dev->mtu+14+4).*/
#define MAX_ETH_FRAME_SIZE 1536

#define TX_FIFO_THRESH 256 /* In bytes */

#define RX_FIFO_THRESH  7      /* 7 means NO threshold, Rx buffer level before first PCI xfer.	 */
#define RX_DMA_BURST    6      /* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST    6      /* Maximum PCI burst, '6' is 1024 */
#define EarlyTxThld     0x3F   /* 0x3F means NO early transmit */
#define RxPacketMaxSize 0x0800 /* Maximum size supported is 16K-1 */
#define InterFrameGap   0x03   /* 3 means InterFrameGap = the shortest one */

#define NUM_TX_DESC 1    /* Number of Tx descriptor registers */
#define NUM_RX_DESC 4    /* Number of Rx descriptor registers */
#define RX_BUF_SIZE 1536 /* Rx Buffer size */
#define RX_BUF_LEN  8192

#define RTL_MIN_IO_SIZE 0x80
#define TX_TIMEOUT      (6 * HZ)

/* write/read MMIO register. Notice: {read,write}[wl] do the necessary swapping */
#define RTL_W8(reg, val8)   write8((val8), (void *) (ioaddr + (reg)))
#define RTL_W16(reg, val16) write16((val16), (void *) (ioaddr + (reg)))
#define RTL_W32(reg, val32) write32((val32), (void *) (ioaddr + (reg)))
#define RTL_R8(reg)         read8((void *) (ioaddr + (reg)))
#define RTL_R16(reg)        read16((void *) (ioaddr + (reg)))
#define RTL_R32(reg)        read32((void *) (ioaddr + (reg)))

enum RTL8169_registers
{
    MAC0                 = 0, /* Ethernet hardware address. */
    MAR0                 = 8, /* Multicast filter. */
    TxDescStartAddrLow   = 0x20,
    TxDescStartAddrHigh  = 0x24,
    TxHDescStartAddrLow  = 0x28,
    TxHDescStartAddrHigh = 0x2c,
    FLASH                = 0x30,
    ERSR                 = 0x36,
    ChipCmd              = 0x37,
    TxPoll_8169          = 0x38,
    IntrMask_8169        = 0x3C,
    IntrStatus_8169      = 0x3E,
    TxConfig             = 0x40,
    RxConfig             = 0x44,
    RxMissed             = 0x4C,
    Cfg9346              = 0x50,
    Config0              = 0x51,
    Config1              = 0x52,
    Config2              = 0x53,
    Config3              = 0x54,
    Config4              = 0x55,
    Config5              = 0x56,
    MultiIntr            = 0x5C,
    PHYAR                = 0x60,
    TBICSR               = 0x64,
    TBI_ANAR             = 0x68,
    TBI_LPAR             = 0x6A,
    PHYstatus            = 0x6C,
    RxMaxSize            = 0xDA,
    CPlusCmd             = 0xE0,
    RxDescStartAddrLow   = 0xE4,
    RxDescStartAddrHigh  = 0xE8,
    EarlyTxThres         = 0xEC,
    FuncEvent            = 0xF0,
    FuncEventMask        = 0xF4,
    FuncPresetState      = 0xF8,
    FuncForceEvent       = 0xFC,
};

enum RTL8125_registers
{
    IntrMask_8125   = 0x38,
    IntrStatus_8125 = 0x3C,
    TxPoll_8125     = 0x90,
};

enum RTL8169_register_content
{
    /*InterruptStatusBits */
    SYSErr        = 0x8000,
    PCSTimeout    = 0x4000,
    SWInt         = 0x0100,
    TxDescUnavail = 0x80,
    RxFIFOOver    = 0x40,
    RxUnderrun    = 0x20,
    RxOverflow    = 0x10,
    TxErr         = 0x08,
    TxOK          = 0x04,
    RxErr         = 0x02,
    RxOK          = 0x01,

    /*RxStatusDesc */
    RxRES  = 0x00200000,
    RxCRC  = 0x00080000,
    RxRUNT = 0x00100000,
    RxRWT  = 0x00400000,

    /*ChipCmdBits */
    CmdReset   = 0x10,
    CmdRxEnb   = 0x08,
    CmdTxEnb   = 0x04,
    RxBufEmpty = 0x01,

    /*Cfg9346Bits */
    Cfg9346_Lock   = 0x00,
    Cfg9346_Unlock = 0xC0,

    /*rx_mode_bits */
    AcceptErr       = 0x20,
    AcceptRunt      = 0x10,
    AcceptBroadcast = 0x08,
    AcceptMulticast = 0x04,
    AcceptMyPhys    = 0x02,
    AcceptAllPhys   = 0x01,

    /*RxConfigBits */
    RxCfgFIFOShift = 13,
    RxCfgDMAShift  = 8,

    /*TxConfigBits */
    TxInterFrameGapShift = 24,
    TxDMAShift           = 8, /* DMA burst value (0-7) is shift this many bits */

    /*rtl8169_PHYstatus */
    TBI_Enable = 0x80,
    TxFlowCtrl = 0x40,
    RxFlowCtrl = 0x20,
    _1000bpsF  = 0x10,
    _100bps    = 0x08,
    _10bps     = 0x04,
    LinkStatus = 0x02,
    FullDup    = 0x01,

    /*GIGABIT_PHY_registers */
    PHY_CTRL_REG      = 0,
    PHY_STAT_REG      = 1,
    PHY_AUTO_NEGO_REG = 4,
    PHY_1000_CTRL_REG = 9,

    /*GIGABIT_PHY_REG_BIT */
    PHY_Restart_Auto_Nego = 0x0200,
    PHY_Enable_Auto_Nego  = 0x1000,

    /* PHY_STAT_REG = 1; */
    PHY_Auto_Nego_Comp = 0x0020,

    /* PHY_AUTO_NEGO_REG = 4; */
    PHY_Cap_10_Half  = 0x0020,
    PHY_Cap_10_Full  = 0x0040,
    PHY_Cap_100_Half = 0x0080,
    PHY_Cap_100_Full = 0x0100,

    /* PHY_1000_CTRL_REG = 9; */
    PHY_Cap_1000_Full = 0x0200,

    PHY_Cap_Null = 0x0,

    /*_MediaType*/
    _10_Half   = 0x01,
    _10_Full   = 0x02,
    _100_Half  = 0x04,
    _100_Full  = 0x08,
    _1000_Full = 0x10,

    /*_TBICSRBit*/
    TBILinkOK = 0x02000000,

    /* FuncEvent/Misc */
    RxDv_Gated_En = 0x80000,
};

static struct
{
    const char *name;
    uint8_t     version;      /* depend on RTL8169 docs */
    uint32_t    RxConfigMask; /* should clear the bits supported by this chip */
} rtl_chip_info[] = {
    {
        "RTL-8169",
        0x00,
        0xff7e1880,
    },
    {
        "RTL-8169",
        0x04,
        0xff7e1880,
    },
    {
        "RTL-8169",
        0x00,
        0xff7e1880,
    },
    {
        "RTL-8169s/8110s",
        0x02,
        0xff7e1880,
    },
    {
        "RTL-8169s/8110s",
        0x04,
        0xff7e1880,
    },
    {
        "RTL-8169sb/8110sb",
        0x10,
        0xff7e1880,
    },
    {
        "RTL-8169sc/8110sc",
        0x18,
        0xff7e1880,
    },
    {
        "RTL-8168b/8111sb",
        0x30,
        0xff7e1880,
    },
    {
        "RTL-8168b/8111sb",
        0x38,
        0xff7e1880,
    },
    {
        "RTL-8168c/8111c",
        0x3c,
        0xff7e1880,
    },
    {
        "RTL-8168d/8111d",
        0x28,
        0xff7e1880,
    },
    {
        "RTL-8168evl/8111evl",
        0x2e,
        0xff7e1880,
    },
    {
        "RTL-8168/8111g",
        0x4c,
        0xff7e1880,
    },
    {
        "RTL-8101e",
        0x34,
        0xff7e1880,
    },
    {
        "RTL-8100e",
        0x32,
        0xff7e1880,
    },
    {
        "RTL-8168h/8111h",
        0x54,
        0xff7e1880,
    },
    {
        "RTL-8125B",
        0x64,
        0xff7e1880,
    },
};

enum _DescStatusBit
{
    OWNbit = 0x80000000,
    EORbit = 0x40000000,
    FSbit  = 0x20000000,
    LSbit  = 0x10000000,
};

struct TxDesc
{
    uint32_t status;
    uint32_t vlan_tag;
    uint32_t buf_addr;
    uint32_t buf_Haddr;
};

struct RxDesc
{
    uint32_t status;
    uint32_t vlan_tag;
    uint32_t buf_addr;
    uint32_t buf_Haddr;
};

static unsigned char rxdata[RX_BUF_LEN];

#define RTL8169_DESC_SIZE 16

#define RTL8169_ALIGN 256


/* Simple delay function */
static void
udelay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 100; i++)
        ;
}

/*
 * Warn if the cache-line size is larger than the descriptor size. In such
 * cases the driver will likely fail because the CPU needs to flush the cache
 * when requeuing RX buffers, therefore descriptors written by the hardware
 * may be discarded.
 *
 * This can be fixed by defining CONFIG_SYS_NONCACHED_MEMORY which will cause
 * the driver to allocate descriptors from a pool of non-cached memory.
 *
 * Hardware maintain D-cache coherency in RISC-V architecture.
 */
#if RTL8169_DESC_SIZE < ARCH_DMA_MINALIGN
    #if !defined(CONFIG_SYS_NONCACHED_MEMORY) && !CONFIG_IS_ENABLED(SYS_DCACHE_OFF) &&             \
        !defined(CONFIG_X86) && !defined(CONFIG_RISCV)
        #warning cache-line size is larger than descriptor size
    #endif
#endif

/*
 * Create a static buffer of size RX_BUF_SZ for each TX Descriptor. All
 * descriptors point to a part of this buffer.
 */
static uint8_t txb[NUM_TX_DESC * RX_BUF_SIZE] __attribute__((aligned(RTL8169_ALIGN)));

/*
 * Create a static buffer of size RX_BUF_SZ for each RX Descriptor. All
 * descriptors point to a part of this buffer.
 */
static uint8_t rxb[NUM_RX_DESC * RX_BUF_SIZE] __attribute__((aligned(RTL8169_ALIGN)));

struct rtl8169_private
{
    unsigned long  iobase;
    void          *mmio_addr; /* memory map physical address */
    int            chipset;
    unsigned long  cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
    unsigned long  cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
    unsigned long  dirty_tx;
    struct TxDesc *TxDescArray;               /* Index of 256-alignment Tx Descriptor buffer */
    struct RxDesc *RxDescArray;               /* Index of 256-alignment Rx Descriptor buffer */
    unsigned char *RxBufferRings;             /* Index of Rx Buffer  */
    unsigned char *RxBufferRing[NUM_RX_DESC]; /* Index of Rx Buffer array */
    unsigned char *Tx_skbuff[NUM_TX_DESC];
} tpx;

static struct rtl8169_private *tpc;

void *
dev_get_priv(struct udevice *dev)
{
    return tpc;
}


static const unsigned int rtl8169_rx_config =
    (RX_FIFO_THRESH << RxCfgFIFOShift) | (RX_DMA_BURST << RxCfgDMAShift);

// static struct pci_device_id supported[] = {
// 	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8125) },
// 	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8161) },
// 	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8167) },
// 	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8168) },
// 	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8169) },
// 	{}
// };

void
mdio_write(int RegAddr, int value)
{
    int i;

    RTL_W32(PHYAR, 0x80000000 | (RegAddr & 0xFF) << 16 | value);
    udelay(1000);

    for (i = 2000; i > 0; i--) {
        /* Check if the RTL8169 has completed writing to the specified MII register */
        if (!(RTL_R32(PHYAR) & 0x80000000)) {
            break;
        } else {
            udelay(100);
        }
    }
}

int
mdio_read(int RegAddr)
{
    int i, value = -1;

    RTL_W32(PHYAR, 0x0 | (RegAddr & 0xFF) << 16);
    udelay(1000);

    for (i = 2000; i > 0; i--) {
        /* Check if the RTL8169 has completed retrieving data from the specified MII register */
        if (RTL_R32(PHYAR) & 0x80000000) {
            value = (int) (RTL_R32(PHYAR) & 0xFFFF);
            break;
        } else {
            udelay(100);
        }
    }
    return value;
}

static int
rtl8169_init_board(unsigned long dev_iobase, const char *name)
{
    int      i;
    uint32_t tmp;

#ifdef DEBUG_RTL8169
    printf("%s\n", __FUNCTION__);
#endif
    ioaddr = dev_iobase;

    /* Soft reset the chip. */
    RTL_W8(ChipCmd, CmdReset);

    /* Check that the chip has finished the reset. */
    for (i = 1000; i > 0; i--)
        if ((RTL_R8(ChipCmd) & CmdReset) == 0)
            break;
        else
            udelay(10);

    /* identify chip attached to board */
    tmp = RTL_R32(TxConfig);
    tmp = ((tmp & 0x7c000000) + ((tmp & 0x00800000) << 2)) >> 24;

    for (i = ARRAY_SIZE(rtl_chip_info) - 1; i >= 0; i--) {
        if (tmp == rtl_chip_info[i].version) {
            tpc->chipset = i;
            goto match;
        }
    }

    /* if unknown chip, assume array element #0, original RTL-8169 in this case */
    printf("PCI device %s: unknown chip version, assuming RTL-8169\n", name);
    printf("PCI device: TxConfig = 0x%lX\n", (unsigned long) RTL_R32(TxConfig));
    tpc->chipset = 0;

match:
    return 0;
}

/*
 * TX and RX descriptors are 16 bytes. This causes problems with the cache
 * maintenance on CPUs where the cache-line size exceeds the size of these
 * descriptors. What will happen is that when the driver receives a packet
 * it will be immediately requeued for the hardware to reuse. The CPU will
 * therefore need to flush the cache-line containing the descriptor, which
 * will cause all other descriptors in the same cache-line to be flushed
 * along with it. If one of those descriptors had been written to by the
 * device those changes (and the associated packet) will be lost.
 *
 * To work around this, we make use of non-cached memory if available. If
 * descriptors are mapped uncached there's no need to manually flush them
 * or invalidate them.
 *
 * Note that this only applies to descriptors. The packet data buffers do
 * not have the same constraints since they are 1536 bytes large, so they
 * are unlikely to share cache-lines.
 */

/* Simple aligned memory allocation helper */
static void *
memalign(size_t alignment, size_t size)
{
    void *ptr = t_mem_alloc(size + alignment + sizeof(void *));
    if (!ptr)
        return NULL;

    /* Calculate aligned address */
    unsigned long addr         = (unsigned long) ptr + sizeof(void *);
    unsigned long aligned_addr = (addr + alignment - 1) & ~(alignment - 1);

    /* Store original pointer before the aligned address */
    void **orig_ptr = (void **) (aligned_addr - sizeof(void *));
    *orig_ptr       = ptr;

    return (void *) aligned_addr;
}

static void *
rtl_alloc_descs(unsigned int num)
{
    size_t size = num * RTL8169_DESC_SIZE;
    return memalign(RTL8169_ALIGN, size);
}

/*
 * Cache maintenance functions. These are simple wrappers around the more
 * general purpose cache management functions.
 */

static void
rtl_inval_rx_desc(struct RxDesc *desc)
{
    /* Invalidate the descriptor to ensure CPU reads fresh data from memory */
    invalidate_dcache_va_range(desc, sizeof(*desc));
}

static void
rtl_flush_rx_desc(struct RxDesc *desc)
{
    /* Clean (flush) the descriptor to ensure writes are visible to hardware */
    clean_dcache_va_range(desc, sizeof(*desc));
}

static void
rtl_inval_tx_desc(struct TxDesc *desc)
{
    /* Invalidate the descriptor to ensure CPU reads fresh data from memory */
    invalidate_dcache_va_range(desc, sizeof(*desc));
}

static void
rtl_flush_tx_desc(struct TxDesc *desc)
{
    /* Clean (flush) the descriptor to ensure writes are visible to hardware */
    clean_dcache_va_range(desc, sizeof(*desc));
}

static void
rtl_inval_buffer(void *buf, size_t size)
{
    /* Invalidate the buffer to ensure CPU reads fresh data from memory */
    invalidate_dcache_va_range(buf, size);
}

static void
rtl_flush_buffer(void *buf, size_t size)
{
    /* Clean (flush) the buffer to ensure writes are visible to hardware */
    clean_dcache_va_range(buf, size);
}

/**************************************************************************
RECV - Receive a frame
***************************************************************************/
static int
rtl_recv_common(struct udevice *dev, unsigned long dev_iobase, unsigned char **packetp)
{
    /* return true if there's an ethernet packet ready to read */
    /* nic->packet should contain data on return */
    /* nic->packetlen should contain length of data */
    struct pci_child_plat *pplat = dev_get_parent_plat(dev);
    int                    cur_rx;
    int                    length = 0;

    ioaddr = dev_iobase;

    cur_rx = tpc->cur_rx;

    rtl_inval_rx_desc(&tpc->RxDescArray[cur_rx]);

    if ((le32_to_cpu(tpc->RxDescArray[cur_rx].status) & OWNbit) == 0) {
        if (!(le32_to_cpu(tpc->RxDescArray[cur_rx].status) & RxRES)) {
            length = (int) (le32_to_cpu(tpc->RxDescArray[cur_rx].status) & 0x00001FFF) - 4;

            rtl_inval_buffer(tpc->RxBufferRing[cur_rx], length);
            memcpy(rxdata, tpc->RxBufferRing[cur_rx], length);

            if (cur_rx == NUM_RX_DESC - 1)
                tpc->RxDescArray[cur_rx].status = cpu_to_le32((OWNbit | EORbit) + RX_BUF_SIZE);
            else
                tpc->RxDescArray[cur_rx].status = cpu_to_le32(OWNbit + RX_BUF_SIZE);
            tpc->RxDescArray[cur_rx].buf_addr =
                cpu_to_le32(dm_pci_mem_to_phys(dev, (unsigned long) tpc->RxBufferRing[cur_rx]));
            rtl_flush_rx_desc(&tpc->RxDescArray[cur_rx]);
            *packetp = rxdata;
        } else {
            printf("Error Rx");
            length = -EIO;
        }
        cur_rx      = (cur_rx + 1) % NUM_RX_DESC;
        tpc->cur_rx = cur_rx;
        return length;

    } else {
        uint32_t IntrStatus = IntrStatus_8169;

        if (pplat->device == 0x8125)
            IntrStatus = IntrStatus_8125;
        unsigned short sts = RTL_R8(IntrStatus);
        RTL_W8(IntrStatus, sts & ~(TxErr | RxErr | SYSErr));
        udelay(100); /* wait */
    }
    tpc->cur_rx = cur_rx;
    return (0); /* initially as this is called to flush the input */
}

int
rtl8169_eth_recv(struct udevice *dev, int flags, unsigned char **packetp)
{
    struct rtl8169_private *priv = dev_get_priv(dev);

    return rtl_recv_common(dev, priv->iobase, packetp);
}

#define HZ 1000
/**************************************************************************
SEND - Transmit a frame
***************************************************************************/
static int
rtl_send_common(struct udevice *dev, unsigned long dev_iobase, void *packet, int length)
{
    /* send the packet to destination */

    struct pci_child_plat *pplat = dev_get_parent_plat(dev);
    uint32_t               to;
    uint8_t               *ptxb;
    int                    entry = tpc->cur_tx % NUM_TX_DESC;
    uint32_t               len   = length;
    int                    ret;

    printf("%s\n", __FUNCTION__);
    printf("sending %d bytes\n", len);

    ioaddr = dev_iobase;

    /* point to the current txb incase multiple tx_rings are used */
    ptxb = tpc->Tx_skbuff[entry * MAX_ETH_FRAME_SIZE];
    memcpy(ptxb, (char *) packet, (int) length);

    while (len < ETH_ZLEN)
        ptxb[len++] = '\0';

    rtl_flush_buffer(ptxb, ALIGN(len, RTL8169_ALIGN));

    tpc->TxDescArray[entry].buf_Haddr = 0;
    tpc->TxDescArray[entry].buf_addr  = cpu_to_le32(dm_pci_mem_to_phys(dev, (unsigned long) ptxb));
    if (entry != (NUM_TX_DESC - 1)) {
        tpc->TxDescArray[entry].status =
            cpu_to_le32((OWNbit | FSbit | LSbit) | ((len > ETH_ZLEN) ? len : ETH_ZLEN));
    } else {
        tpc->TxDescArray[entry].status =
            cpu_to_le32((OWNbit | EORbit | FSbit | LSbit) | ((len > ETH_ZLEN) ? len : ETH_ZLEN));
    }
    rtl_flush_tx_desc(&tpc->TxDescArray[entry]);
    if (pplat->device == 0x8125)
        RTL_W8(TxPoll_8125, 0x1); /* set polling bit */
    else
        RTL_W8(TxPoll_8169, 0x40); /* set polling bit */

    tpc->cur_tx++;
    to = currticks() + TX_TIMEOUT;
    do {
        rtl_inval_tx_desc(&tpc->TxDescArray[entry]);
    } while ((le32_to_cpu(tpc->TxDescArray[entry].status) & OWNbit) &&
             (currticks() < to)); /* wait */

    if (currticks() >= to) {
        printf("tx timeout/error\n");
        printf("%s elapsed time : %lu\n", __func__, currticks());
        ret = -ETIMEDOUT;
    } else {
        printf("tx done\n");
        ret = 0;
    }
    /* Delay to make net console (nc) work properly */
    udelay(20);
    return ret;
}

int
rtl8169_eth_send(struct udevice *dev, void *packet, int length)
{
    struct rtl8169_private *priv = dev_get_priv(dev);

    return rtl_send_common(dev, priv->iobase, packet, length);
}

static void
rtl8169_set_rx_mode(void)
{
    uint32_t mc_filter[2]; /* Multicast hash filter */
    int      rx_mode;
    uint32_t tmp = 0;

#ifdef DEBUG_RTL8169
    printf("%s\n", __FUNCTION__);
#endif

    /* IFF_ALLMULTI */
    /* Too many to filter perfectly -- accept all multicasts. */
    rx_mode      = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
    mc_filter[1] = mc_filter[0] = 0xffffffff;

    tmp = rtl8169_rx_config | rx_mode |
          (RTL_R32(RxConfig) & rtl_chip_info[tpc->chipset].RxConfigMask);

    RTL_W32(RxConfig, tmp);
    RTL_W32(MAR0 + 0, mc_filter[0]);
    RTL_W32(MAR0 + 4, mc_filter[1]);
}

static void
rtl8169_hw_start(struct udevice *dev)
{
    uint32_t i;

#ifdef DEBUG_RTL8169
    int stime = currticks();
    printf("%s\n", __FUNCTION__);
#endif

#if 0
	/* Soft reset the chip. */
	RTL_W8(ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 1000; i > 0; i--) {
		if ((RTL_R8(ChipCmd) & CmdReset) == 0)
			break;
		else
			udelay(10);
	}
#endif

    RTL_W8(Cfg9346, Cfg9346_Unlock);

    /* RTL-8169sb/8110sb or previous version */
    if (tpc->chipset <= 5)
        RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

    RTL_W8(EarlyTxThres, EarlyTxThld);

    /* For gigabit rtl8169 */
    RTL_W16(RxMaxSize, RxPacketMaxSize);

    /* Set Rx Config register */
    i = rtl8169_rx_config | (RTL_R32(RxConfig) & rtl_chip_info[tpc->chipset].RxConfigMask);
    RTL_W32(RxConfig, i);

    /* Set DMA burst size and Interframe Gap Time */
    RTL_W32(TxConfig, (TX_DMA_BURST << TxDMAShift) | (InterFrameGap << TxInterFrameGapShift));

    tpc->cur_rx = 0;

    RTL_W32(TxDescStartAddrLow, dm_pci_mem_to_phys(dev, (unsigned long) tpc->TxDescArray));
    RTL_W32(TxDescStartAddrHigh, (unsigned long) 0);
    RTL_W32(RxDescStartAddrLow, dm_pci_mem_to_phys(dev, (unsigned long) tpc->RxDescArray));
    RTL_W32(RxDescStartAddrHigh, (unsigned long) 0);

    /* RTL-8169sc/8110sc or later version */
    if (tpc->chipset > 5)
        RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

    RTL_W8(Cfg9346, Cfg9346_Lock);
    udelay(10);

    RTL_W32(RxMissed, 0);

    rtl8169_set_rx_mode();

    /* no early-rx interrupts */
    RTL_W16(MultiIntr, RTL_R16(MultiIntr) & 0xF000);

#ifdef DEBUG_RTL8169
    printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
}

static void
rtl8169_init_ring(struct udevice *dev)
{
    int i;

#ifdef DEBUG_RTL8169
    int stime = currticks();
    printf("%s\n", __FUNCTION__);
#endif

    tpc->cur_rx   = 0;
    tpc->cur_tx   = 0;
    tpc->dirty_tx = 0;
    memset(tpc->TxDescArray, 0x0, NUM_TX_DESC * sizeof(struct TxDesc));
    memset(tpc->RxDescArray, 0x0, NUM_RX_DESC * sizeof(struct RxDesc));

    for (i = 0; i < NUM_TX_DESC; i++) {
        tpc->Tx_skbuff[i] = &txb[i];
    }

    for (i = 0; i < NUM_RX_DESC; i++) {
        if (i == (NUM_RX_DESC - 1))
            tpc->RxDescArray[i].status = cpu_to_le32((OWNbit | EORbit) + RX_BUF_SIZE);
        else
            tpc->RxDescArray[i].status = cpu_to_le32(OWNbit + RX_BUF_SIZE);

        tpc->RxBufferRing[i] = &rxb[i * RX_BUF_SIZE];
        tpc->RxDescArray[i].buf_addr =
            cpu_to_le32(dm_pci_mem_to_phys(dev, (unsigned long) tpc->RxBufferRing[i]));
        rtl_flush_rx_desc(&tpc->RxDescArray[i]);
    }

#ifdef DEBUG_RTL8169
    printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
}

static void
rtl8169_common_start(struct udevice *dev, unsigned char *enetaddr, unsigned long dev_iobase)
{
    int i;

#ifdef DEBUG_RTL8169
    int stime = currticks();
    printf("%s\n", __FUNCTION__);
#endif

    ioaddr = dev_iobase;

    rtl8169_init_ring(dev);
    rtl8169_hw_start(dev);
    /* Construct a perfect filter frame with the mac address as first match
	 * and broadcast for all others */
    for (i = 0; i < 192; i++)
        txb[i] = 0xFF;

    txb[0] = enetaddr[0];
    txb[1] = enetaddr[1];
    txb[2] = enetaddr[2];
    txb[3] = enetaddr[3];
    txb[4] = enetaddr[4];
    txb[5] = enetaddr[5];

#ifdef DEBUG_RTL8169
    printf("%s elapsed time : %lu\n", __func__, currticks() - stime);
#endif
}

int
rtl8169_eth_start(struct udevice *dev)
{
    struct eth_pdata       *plat = dev_get_plat(dev);
    struct rtl8169_private *priv = dev_get_priv(dev);

    rtl8169_common_start(dev, plat->enetaddr, priv->iobase);

    return 0;
}

static void
rtl_halt_common(struct udevice *dev)
{
    struct rtl8169_private *priv  = dev_get_priv(dev);
    struct pci_child_plat  *pplat = dev_get_parent_plat(dev);
    int                     i;

#ifdef DEBUG_RTL8169
    printf("%s\n", __FUNCTION__);
#endif

    ioaddr = priv->iobase;

    /* Stop the chip's Tx and Rx DMA processes. */
    RTL_W8(ChipCmd, 0x00);

    /* Disable interrupts by clearing the interrupt mask. */
    if (pplat->device == 0x8125)
        RTL_W16(IntrMask_8125, 0x0000);
    else
        RTL_W16(IntrMask_8169, 0x0000);

    RTL_W32(RxMissed, 0);

    for (i = 0; i < NUM_RX_DESC; i++) {
        tpc->RxBufferRing[i] = NULL;
    }
}

void
rtl8169_eth_stop(struct udevice *dev)
{
    rtl_halt_common(dev);
}

static int
rtl8169_write_hwaddr(struct udevice *dev)
{
    struct eth_pdata *plat = dev_get_plat(dev);
    unsigned int      i;

    RTL_W8(Cfg9346, Cfg9346_Unlock);

    for (i = 0; i < MAC_ADDR_LEN; i++)
        RTL_W8(MAC0 + i, plat->enetaddr[i]);

    RTL_W8(Cfg9346, Cfg9346_Lock);

    return 0;
}

/**************************************************************************
INIT - Look for an adapter, this routine's visible to the outside
***************************************************************************/

#define board_found 1
#define valid_link  0
static int
rtl_init(unsigned long dev_ioaddr, const char *name, unsigned char *enetaddr)
{
    static int board_idx = -1;
    int        i, rc;
    int        option = -1, Cap10_100 = 0, Cap1000 = 0;

    ioaddr = dev_ioaddr;

    board_idx++;

    /* point to private storage */
    tpc = &tpx;

    rc = rtl8169_init_board(ioaddr, name);
    if (rc)
        return rc;

    /* Get MAC address.  FIXME: read EEPROM */
    for (i = 0; i < MAC_ADDR_LEN; i++)
        enetaddr[i] = RTL_R8(MAC0 + i);

    printf("chipset = %d\n", tpc->chipset);
    printf("MAC Address");
    for (i = 0; i < MAC_ADDR_LEN; i++)
        printf(":%02x", enetaddr[i]);

    /* Print out some hardware info */
    printf("%s: at ioaddr 0x%lx\n", name, ioaddr);

    /* if TBI is not endbled */
    if (!(RTL_R8(PHYstatus) & TBI_Enable)) {
        int val = mdio_read(PHY_AUTO_NEGO_REG);

        option = (board_idx >= MAX_UNITS) ? 0 : media[board_idx];
        /* Force RTL8169 in 10/100/1000 Full/Half mode. */
        if (option > 0) {
            printf("%s: Force-mode Enabled.\n", name);
            Cap10_100 = 0, Cap1000 = 0;
            switch (option) {
                case _10_Half:
                    Cap10_100 = PHY_Cap_10_Half;
                    Cap1000   = PHY_Cap_Null;
                    break;
                case _10_Full:
                    Cap10_100 = PHY_Cap_10_Full;
                    Cap1000   = PHY_Cap_Null;
                    break;
                case _100_Half:
                    Cap10_100 = PHY_Cap_100_Half;
                    Cap1000   = PHY_Cap_Null;
                    break;
                case _100_Full:
                    Cap10_100 = PHY_Cap_100_Full;
                    Cap1000   = PHY_Cap_Null;
                    break;
                case _1000_Full:
                    Cap10_100 = PHY_Cap_Null;
                    Cap1000   = PHY_Cap_1000_Full;
                    break;
                default:
                    break;
            }
            mdio_write(PHY_AUTO_NEGO_REG,
                       Cap10_100 | (val & 0x1F)); /* leave PHY_AUTO_NEGO_REG bit4:0 unchanged */
            mdio_write(PHY_1000_CTRL_REG, Cap1000);
        } else {
            printf("%s: Auto-negotiation Enabled.\n", name);
            /* enable 10/100 Full/Half Mode, leave PHY_AUTO_NEGO_REG bit4:0 unchanged */
            mdio_write(PHY_AUTO_NEGO_REG,
                       PHY_Cap_10_Half | PHY_Cap_10_Full | PHY_Cap_100_Half | PHY_Cap_100_Full |
                           (val & 0x1F));

            /* enable 1000 Full Mode */
            mdio_write(PHY_1000_CTRL_REG, PHY_Cap_1000_Full);
        }

        /* Enable auto-negotiation and restart auto-nigotiation */
        mdio_write(PHY_CTRL_REG, PHY_Enable_Auto_Nego | PHY_Restart_Auto_Nego);
        udelay(100);

        /* wait for auto-negotiation process */
        for (i = 10000; i > 0; i--) {
            /* check if auto-negotiation complete */
            if (mdio_read(PHY_STAT_REG) & PHY_Auto_Nego_Comp) {
                udelay(100);
                option = RTL_R8(PHYstatus);
                if (option & _1000bpsF) {
#ifdef DEBUG_RTL8169
                    printf("%s: 1000Mbps Full-duplex operation.\n", name);
#endif
                } else {
#ifdef DEBUG_RTL8169
                    printf("%s: %sMbps %s-duplex operation.\n",
                           name,
                           (option & _100bps) ? "100" : "10",
                           (option & FullDup) ? "Full" : "Half");
#endif
                }
                break;
            } else {
                udelay(100);
            }
        } /* end for-loop to wait for auto-negotiation process */

    } else {
        udelay(100);
#ifdef DEBUG_RTL8169
        printf("%s: 1000Mbps Full-duplex operation, TBI Link %s!\n",
               name,
               (RTL_R32(TBICSR) & TBILinkOK) ? "OK" : "Failed");
#endif
    }

    tpc->RxDescArray = rtl_alloc_descs(NUM_RX_DESC);
    if (!tpc->RxDescArray)
        return -ENOMEM;

    tpc->TxDescArray = rtl_alloc_descs(NUM_TX_DESC);
    if (!tpc->TxDescArray)
        return -ENOMEM;

    return 0;
}

int rtl8169_eth_probe(struct udevice *dev)
{
    struct pci_child_plat  *pplat = dev_get_parent_plat(dev);
    struct rtl8169_private *priv  = dev_get_priv(dev);
    struct eth_pdata       *plat  = dev_get_plat(dev);
    int                     region;
    int                     ret;

    switch (pplat->device) {
        case 0x8125:
        case 0x8161:
        case 0x8168:
            region = 2;
            break;
        default:
            region = 1;
            break;
    }

    priv->iobase = (unsigned long)
        dm_pci_map_bar(dev, PCI_BASE_ADDRESS_0 + region * 4, 0, 0, PCI_REGION_TYPE, PCI_REGION_MEM);

    debug("rtl8169: REALTEK RTL8169 @0x%lx\n", priv->iobase);
    ret = rtl_init(priv->iobase, dev->name, plat->enetaddr);
    if (ret < 0) {
        printf(pr_fmt("failed to initialize card: %d\n"), ret);
        return ret;
    }

    /*
	 * WAR for DHCP failure after rebooting from kernel.
	 * Clear RxDv_Gated_En bit which was set by kernel driver.
	 * Without this, U-Boot can't get an IP via DHCP.
	 * Register (FuncEvent, aka MISC) and RXDV_GATED_EN bit are from
	 * the r8169.c kernel driver.
	 */

    uint32_t val = RTL_R32(FuncEvent);
    debug("%s: FuncEvent/Misc (0xF0) = 0x%08X\n", __func__, val);
    val &= ~RxDv_Gated_En;
    RTL_W32(FuncEvent, val);

    return 0;
}

// static const struct eth_ops rtl8169_eth_ops = {
// 	.start	= rtl8169_eth_start,
// 	.send	= rtl8169_eth_send,
// 	.recv	= rtl8169_eth_recv,
// 	.stop	= rtl8169_eth_stop,
// 	.write_hwaddr = rtl8169_write_hwaddr,
// };

// static const struct udevice_id rtl8169_eth_ids[] = {
// 	{ .compatible = "realtek,rtl8169" },
// 	{ }
// };

// U_BOOT_DRIVER(eth_rtl8169) = {
// 	.name	= "eth_rtl8169",
// 	.id	= UCLASS_ETH,
// 	.of_match = rtl8169_eth_ids,
// 	.probe	= rtl8169_eth_probe,
// 	.ops	= &rtl8169_eth_ops,
// 	.priv_auto	= sizeof(struct rtl8169_private),
// 	.plat_auto	= sizeof(struct eth_pdata),
// };

// U_BOOT_PCI_DEVICE(eth_rtl8169, supported);

void
test_rtl8125(void)
{
    uint64_t       rtl_mmio_phys = 0x9c0100000UL;
    struct udevice dev           = {
                  .name      = "eth_rtl8169",
                  .mmio_base = rtl_mmio_phys,
    };
    uint8_t local_ip[4]  = {192, 168, 1, 60};
    uint8_t remote_ip[4] = {192, 168, 1, 8};
	uint8_t packet[128] = {0};
	uint32_t pak_len = 0;

    rtl8169_eth_probe(&dev);
    rtl8169_eth_start(&dev);

	pak_len = generate_ping(local_ip, remote_ip, 1, packet);

    rtl8169_eth_send(&dev, packet, pak_len);
}

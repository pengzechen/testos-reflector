# RTL8125 PCIe 驱动快速参考

## 快速开始

### 1. 最简单的使用方式

```c
#include "include/dev/pcie_test.h"

void main(void) {
    test_dw_pcie_atu();
}
```

### 2. 关键地址配置

| 名称 | 地址 | 说明 |
|------|------|------|
| DBI_BASE | 0xa40c00000 | PCIe DBI 寄存器基地址 |
| MMIO_BASE | 0xf3000000 | PCIe 配置空间窗口 |
| PHY_ADDR | 0x40100000 | 物理内存起始地址 |
| RTL_MMIO | 0x9c0100000 | RTL8125 MMIO 地址 |

### 3. 网络配置

默认配置：
- **本地 IP**: 192.168.22.102
- **目标 IP**: 192.168.22.101 (ping 目标)
- **MAC 地址**: 从网卡读取

## 日志级别

| 函数 | 用途 |
|------|------|
| `logger_info()` | 重要流程信息 |
| `logger_debug()` | 详细调试信息 |
| `logger_warn()` | 警告信息 |
| `logger_error()` | 错误信息 |

## 主要函数

### dw_pcie_setup_atu()
配置 PCIe 地址转换单元

```c
int dw_pcie_setup_atu(
    uint64_t dbi_base,      // DBI 寄存器基地址
    uint32_t region_index,  // ATU 区域索引 (0-15)
    uint32_t type,          // 类型: MEM 或 CFG0
    uint64_t cpu_addr,      // CPU 侧地址
    uint64_t pci_addr,      // PCIe 侧地址
    uint64_t size           // 窗口大小
);
```

### pcie_scan_bus()
扫描 PCIe 总线查找设备

```c
int pcie_scan_bus(
    uint64_t cfg_base,      // 配置空间基地址
    uint32_t *vendor_id,    // 输出: Vendor ID
    uint32_t *device_id,    // 输出: Device ID
    uint32_t *class_code    // 输出: Class Code
);
```

### pcie_get_bar_info()
读取设备 BAR 信息

```c
int pcie_get_bar_info(
    uint64_t cfg_base,      // 配置空间基地址
    uint32_t bar_num,       // BAR 编号 (0-5)
    uint64_t *bar_addr,     // 输出: BAR 地址
    uint64_t *bar_size      // 输出: BAR 大小
);
```

### rtl8125_init()
初始化 RTL8125 网卡

```c
int rtl8125_init(
    uint64_t mmio_base      // RTL8125 MMIO 基地址
);
```

### send_ping()
发送 ICMP Echo Request

```c
void send_ping(
    uint8_t src_ip[4],      // 源 IP 地址
    uint8_t dst_ip[4],      // 目标 IP 地址
    uint16_t seq            // 序列号
);
```

## RTL8125 寄存器

| 寄存器 | 偏移 | 说明 |
|--------|------|------|
| MAC0 | 0x0000 | MAC 地址字节 0-3 |
| MAC4 | 0x0004 | MAC 地址字节 4-5 |
| ChipCmd | 0x0037 | 芯片命令寄存器 |
| TxConfig | 0x0040 | TX 配置 |
| RxConfig | 0x0044 | RX 配置 |
| Cfg9346 | 0x0050 | 配置寄存器锁 |
| TxPoll | 0x0090 | TX 轮询 |
| TxDescStartAddr | 0x0020 | TX 描述符地址 |
| RxDescStartAddr | 0x00E4 | RX 描述符地址 |
| IntrMask | 0x0038 | 中断屏蔽 |
| IntrStatus | 0x003C | 中断状态 |

## 芯片命令位

| 位 | 值 | 说明 |
|----|----|----|
| CMD_RESET | 0x10 | 软件复位 |
| CMD_RX_ENABLE | 0x08 | 使能接收 |
| CMD_TX_ENABLE | 0x04 | 使能发送 |

## 配置寄存器锁

| 值 | 说明 |
|----|------|
| CFG9346_UNLOCK | 0xC0 | 解锁配置寄存器 |
| CFG9346_LOCK | 0x00 | 锁定配置寄存器 |

## 描述符位

| 位 | 值 | 说明 |
|----|----|----|
| DESC_OWN | 0x80000000 | 所有权位 (1=NIC拥有) |
| DESC_EOR | 0x40000000 | 环结束标志 |
| DESC_FS | 0x20000000 | 帧起始 |
| DESC_LS | 0x10000000 | 帧结束 |

## 网络协议常量

### 以太网
- `ETH_ALEN`: 6 (MAC 地址长度)
- `ETH_HLEN`: 14 (以太网头长度)
- `ETH_P_IP`: 0x0800 (IP 协议)
- `ETH_P_ARP`: 0x0806 (ARP 协议)

### IP
- `IPPROTO_ICMP`: 1 (ICMP 协议号)

### ICMP
- `ICMP_ECHO`: 8 (Echo Request)
- `ICMP_ECHOREPLY`: 0 (Echo Reply)

## 数据结构

### 以太网头
```c
typedef struct {
    uint8_t  dest[6];      // 目标 MAC
    uint8_t  src[6];       // 源 MAC
    uint16_t proto;        // 协议类型
} eth_hdr_t;
```

### IP 头
```c
typedef struct {
    uint8_t  version_ihl;  // 版本和头长度
    uint8_t  tos;          // 服务类型
    uint16_t total_len;    // 总长度
    uint16_t id;           // 标识
    uint16_t frag_off;     // 分片偏移
    uint8_t  ttl;          // 生存时间
    uint8_t  protocol;     // 协议
    uint16_t checksum;     // 校验和
    uint32_t src_addr;     // 源地址
    uint32_t dest_addr;    // 目标地址
} ip_hdr_t;
```

### ICMP 头
```c
typedef struct {
    uint8_t  type;         // 类型
    uint8_t  code;         // 代码
    uint16_t checksum;     // 校验和
    uint16_t id;           // 标识符
    uint16_t sequence;     // 序列号
} icmp_hdr_t;
```

### RTL 描述符
```c
typedef struct {
    uint32_t status;       // 状态和长度
    uint32_t vlan_tag;     // VLAN 标签
    uint32_t buf_addr_lo;  // 缓冲区地址低 32 位
    uint32_t buf_addr_hi;  // 缓冲区地址高 32 位
} rtl_desc_t;
```

## 辅助函数

### MMIO 访问
```c
uint8_t  rtl_read8(uint32_t reg);
uint16_t rtl_read16(uint32_t reg);
uint32_t rtl_read32(uint32_t reg);
void     rtl_write8(uint32_t reg, uint8_t val);
void     rtl_write16(uint32_t reg, uint16_t val);
void     rtl_write32(uint32_t reg, uint32_t val);
```

### 延时
```c
void udelay(uint32_t us);  // 微秒延时
void mdelay(uint32_t ms);  // 毫秒延时
```

### 字节序转换
```c
uint16_t htons(uint16_t val);  // Host to Network Short
uint32_t htonl(uint32_t val);  // Host to Network Long
uint16_t ntohs(uint16_t val);  // Network to Host Short
uint32_t ntohl(uint32_t val);  // Network to Host Long
```

### 校验和
```c
uint16_t ip_checksum(void *data, int len);
```

## 典型的执行流程

1. **ATU 配置**
   - 设置视口选择寄存器
   - 配置基地址和限制地址
   - 配置目标地址
   - 使能 ATU 区域
   - 等待使能完成

2. **设备扫描**
   - 读取 Vendor ID 和 Device ID
   - 验证设备存在
   - 读取 Class Code
   - 识别设备类型

3. **BAR 配置**
   - 读取原始 BAR 值
   - 写入全 1 检测大小
   - 恢复原始值
   - 计算 BAR 大小和地址

4. **网卡初始化**
   - 软件复位
   - 读取 MAC 地址
   - 解锁配置寄存器
   - 配置 TX/RX
   - 设置描述符地址
   - 使能 TX/RX
   - 锁定配置寄存器

5. **发送 Ping**
   - 构造以太网头
   - 构造 IP 头
   - 构造 ICMP 头
   - 计算校验和
   - 发送数据包

6. **接收 Reply**
   - 检查 RX 描述符
   - 读取数据包
   - 解析以太网头
   - 解析 IP 头
   - 解析 ICMP 头
   - 验证是 Echo Reply

## 常见问题

### Q: ATU 配置失败？
A: 检查 DBI_BASE 地址、确认 PCIe 控制器已初始化

### Q: 无法检测到设备？
A: 检查 PCIe 链路状态、验证配置空间可访问

### Q: 网卡初始化失败？
A: 检查 MMIO 地址映射、确认复位成功

### Q: 无法发送数据包？
A: 确认 DMA 缓冲区已分配、TX 使能位已设置

### Q: 无法接收数据包？
A: 确认 RX 描述符已配置、RX 使能位已设置

## 性能调优

### 建议的描述符数量
- TX: 4-256 个描述符
- RX: 4-512 个描述符

### 建议的缓冲区大小
- TX: 2048 字节
- RX: 2048 字节

### DMA 突发长度
- 建议值: 6 (1024 字节)

## 调试命令

### 读取寄存器
```c
uint32_t cmd = rtl_read8(RTL8125_ChipCmd);
logger_debug("ChipCmd = 0x%02x\n", cmd);
```

### 检查 TX/RX 状态
```c
uint32_t status = tx_ring[idx].status;
logger_debug("TX desc[%d] status = 0x%08x\n", idx, status);
logger_debug("  OWN=%d EOR=%d FS=%d LS=%d\n",
    !!(status & DESC_OWN),
    !!(status & DESC_EOR),
    !!(status & DESC_FS),
    !!(status & DESC_LS));
```

### 打印数据包
```c
logger_debug("Packet dump:\n");
for (int i = 0; i < len; i++) {
    if (i % 16 == 0) logger_debug("%04x: ", i);
    logger_debug("%02x ", packet[i]);
    if ((i + 1) % 16 == 0) logger_debug("\n");
}
logger_debug("\n");
```

## 参考资料

- PCIe Base Specification v3.0+
- Realtek RTL8125B Datasheet
- RK3588 Technical Reference Manual
- U-Boot rtl8169.c driver source

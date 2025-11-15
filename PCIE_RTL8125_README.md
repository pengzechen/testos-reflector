# PCIe ATU 和 RTL8125 网卡驱动测试

## 概述

本实现提供了完整的 PCIe ATU (Address Translation Unit) 配置和 RTL8125 网卡驱动，包括 ICMP ping 功能。这是基于 U-Boot 的 rtl8169.c 驱动改编而来，适用于 RK3588 平台的 testos-reflector 项目。

## 功能特性

### 1. PCIe ATU 配置
- 配置 DesignWare PCIe 控制器的地址转换单元
- 支持内存和配置空间访问类型
- 详细的寄存器配置日志输出

### 2. PCIe 设备枚举
- 扫描 PCIe 总线查找连接的设备
- 读取设备的 Vendor ID、Device ID 和 Class Code
- 自动识别 RealTek RTL8125/RTL8169 网卡

### 3. BAR 信息读取
- 读取设备的 Base Address Register (BAR) 信息
- 支持 I/O 和内存类型的 BAR
- 支持 32 位和 64 位 BAR
- 自动计算 BAR 的大小

### 4. RTL8125 驱动初始化
- 软件复位网卡
- 读取 MAC 地址
- 配置 TX/RX 缓冲区和描述符环
- 配置网卡工作模式

### 5. 网络功能 (ICMP Ping)
- 构造以太网帧
- 构造 IP 数据包
- 构造 ICMP Echo Request
- 发送 ping 请求
- 接收和解析 ping 回复

## 文件结构

```
testos-reflector/
├── pcie_dw_rockchip.c          # 主实现文件（在文件末尾添加了测试代码）
├── include/
│   └── dev/
│       └── pcie_test.h          # 头文件声明
└── src/
    └── test_pcie_network.c      # 测试主程序
```

## 使用方法

### 1. 集成到项目

在你的主入口函数（如 `src/t_entry.c`）中调用测试函数：

```c
#include "include/dev/pcie_test.h"

void main(void) {
    // ... 其他初始化代码 ...
    
    // 调用 PCIe 和网络测试
    test_dw_pcie_atu();
    
    // ... 其他代码 ...
}
```

或者使用提供的包装函数：

```c
extern void pcie_network_test_main(void);

void main(void) {
    pcie_network_test_main();
}
```

### 2. 编译

确保在 Makefile 中包含相关文件：

```makefile
SOURCES += pcie_dw_rockchip.c
SOURCES += src/test_pcie_network.c
```

### 3. 运行

系统启动后会自动执行 PCIe 测试并输出详细日志。

## 地址映射

根据 Rust 示例代码，使用以下地址映射：

- **DBI Base**: `0xa40c00000` - PCIe DBI 寄存器基地址
- **MMIO Base**: `0xf3000000` - 配置空间窗口
- **Physical Start**: `0x40100000` - 物理内存起始地址
- **RTL MMIO**: `0x9c0100000` - RTL8125 设备 MMIO 地址

这些地址可以根据实际硬件配置进行调整。

## 日志输出

程序提供了详细的日志输出，包括：

### ATU 配置阶段
```
=== Setting up PCIe ATU Region 0 ===
  Type: 0x4 (Config)
  CPU Address: 0xf3000000
  PCI Address: 0x00000000
  Size: 0x100000 (1048576 bytes)
  Set viewport to region 0
  Lower base: 0xf3000000
  Upper base: 0x0
  Limit: 0xf30fffff
  ...
ATU region 0 enabled successfully!
```

### 设备扫描阶段
```
=== Scanning PCIe Bus ===
  Config base: 0xf3000000
  Vendor ID: 0x10ec
  Device ID: 0x8125
  Class Code: 0x020000
  Device identified: RealTek (0x10EC)
  Model: RTL8125 2.5GbE Controller
```

### 网卡初始化阶段
```
=== Initializing RTL8125 Network Controller ===
  MMIO Base: 0x9c0100000
  Reading MAC address...
  MAC Address: 00:e0:4c:68:12:34
  Performing software reset...
  Reset completed
  ...
RTL8125 initialization complete!
```

### Ping 测试阶段
```
=== Preparing ICMP Echo Request (Ping) ===
  Source IP: 192.168.22.102
  Destination IP: 192.168.22.101
  Sequence: 1
  Ethernet header:
    Dest MAC: ff:ff:ff:ff:ff:ff
    Src MAC: 00:e0:4c:68:12:34
    EtherType: 0x0800 (IP)
  IP header:
    Version: 4, Header length: 20 bytes
    Total length: 60 bytes
    TTL: 64
    Protocol: 1 (ICMP)
    Checksum: 0x...
  ICMP header:
    Type: 8 (Echo Request)
    Code: 0
    Checksum: 0x...
    ID: 0x5678
    Sequence: 1
  Total packet size: 74 bytes
  Sending ping packet...
Ping request sent successfully!
```

## 注意事项

### 1. DMA 内存分配

当前实现中，TX/RX 描述符和缓冲区的内存分配是注释掉的。在实际使用中，你需要：

- 分配物理连续的 DMA 内存区域
- 确保这些区域对设备可见（需要正确的内存映射）
- 处理缓存一致性问题

示例：
```c
// 分配 DMA 内存（需要实现你自己的 DMA 分配器）
tx_ring = (rtl_desc_t *)dma_alloc_coherent(sizeof(rtl_desc_t) * NUM_TX_DESC);
rx_ring = (rtl_desc_t *)dma_alloc_coherent(sizeof(rtl_desc_t) * NUM_RX_DESC);
```

### 2. 地址转换

代码中假设使用恒等映射（identity mapping），即物理地址 = 虚拟地址。如果你的系统使用 MMU，需要：

```c
// 实现或使用现有的地址转换函数
uint64_t virt_addr = phys_to_virt(phys_addr);
uint64_t phys_addr = virt_to_phys(virt_addr);
```

### 3. 中断处理

当前实现是轮询模式。对于生产环境，建议添加中断处理：

- 配置 GICv3 中断控制器
- 注册 PCIe 设备的中断处理函数
- 实现 RX 中断处理逻辑

### 4. ARP 协议

Ping 功能当前使用广播 MAC 地址。完整的实现需要：

- 实现 ARP 请求/应答
- 维护 ARP 缓存表
- 在发送 IP 包前解析目标 MAC 地址

### 5. PHY 配置

RTL8125 的 PHY (物理层) 配置在当前实现中被简化了。完整的实现需要：

- 通过 MDIO 接口配置 PHY
- 设置链路速度和双工模式
- 等待链路建立（link up）

## 扩展功能

可以在此基础上扩展以下功能：

### 1. 完整的网络协议栈
- ARP 协议
- UDP 协议
- TCP 协议
- DHCP 客户端

### 2. 多设备支持
- 支持多个 RTL 网卡
- 设备管理器

### 3. 性能优化
- 使用中断代替轮询
- 实现 scatter-gather DMA
- 启用 TCP/IP offload 功能

### 4. 诊断工具
- 网卡统计信息
- 错误计数器
- 性能监控

## 调试技巧

### 1. 使用日志级别

代码使用不同的日志级别：
- `logger_info()` - 重要信息
- `logger_debug()` - 调试细节
- `logger_warn()` - 警告信息
- `logger_error()` - 错误信息

### 2. 寄存器转储

添加寄存器读取来诊断问题：
```c
uint32_t val = rtl_read32(REG_OFFSET);
logger_debug("Register 0x%x = 0x%08x\n", REG_OFFSET, val);
```

### 3. 数据包捕获

打印数据包内容进行分析：
```c
for (int i = 0; i < len; i++) {
    if (i % 16 == 0) logger_debug("\n%04x: ", i);
    logger_debug("%02x ", data[i]);
}
logger_debug("\n");
```

## 参考资料

- **U-Boot RTL8169 驱动**: `rtl8169.c`
- **DesignWare PCIe 驱动**: `pcie_dw_rockchip.c`
- **RK3588 TRM**: `rk3588-TRM-and-Datasheet/`
- **PCIe 规范**: PCI Express Base Specification
- **RTL8125 数据手册**: Realtek RTL8125 Datasheet

## 故障排除

### 问题：ATU 配置失败
- 检查 DBI 基地址是否正确
- 确认 PCIe 控制器已初始化
- 验证时钟和复位信号

### 问题：无法检测到设备
- 检查 PCIe 链路状态（link up）
- 验证配置空间访问是否正常
- 确认设备供电正常

### 问题：网卡初始化失败
- 检查 BAR 地址映射
- 验证 MMIO 访问权限
- 确认复位时序

### 问题：无法发送/接收数据包
- 检查 DMA 缓冲区地址
- 验证描述符配置
- 确认 TX/RX 使能位

## 许可证

本代码基于 U-Boot 的 GPL-2.0+ 许可证，保持相同的许可证条款。

## 作者

改编自 U-Boot rtl8169.c 驱动，适配到 testos-reflector 项目。

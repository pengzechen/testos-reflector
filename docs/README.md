# TestOS — RK3588 NPU 裸机测试

## 概述

TestOS 是一个运行在 RK3588 (Orange Pi 5 Plus) 上的裸机操作系统，用于直接操作 NPU 硬件寄存器进行算子开发和验证。不依赖 Linux 内核和 librknnrt 运行时。

## 编译

### 环境要求

- `aarch64-linux-musl-gcc` 交叉编译工具链
- WSL (Windows Subsystem for Linux) 或原生 Linux 环境
- `mkimage` (u-boot-tools)

### 编译命令

```bash
# 在 WSL 中执行
cd /mnt/c/Users/ajax/Desktop/ida/testos-reflector
make clean && make

# 生成 U-Boot 可加载镜像
make uimg
```

或者从 Windows PowerShell 一行执行：

```powershell
wsl bash -ic "cd /mnt/c/Users/ajax/Desktop/ida/testos-reflector && make clean && make && make uimg"
```

### 编译产物

| 文件 | 说明 |
|------|------|
| `build/testos.elf` | ELF 格式内核（含调试符号） |
| `build/testos.bin` | 裸二进制内核 |
| `testos-reflector-src_aarch64-opi5p.uimg` | U-Boot 可引导镜像 |

### 烧录运行

通过 TFTP + U-Boot 网络引导：

```
# 在 U-Boot 命令行：
tftpboot 0x400000 kernel.uimg
bootm 0x400000
```

## 测试列表

所有测试在 `t_kernel_main()` 中顺序执行。每个测试独立调用 NPU 并验证输出。

| 测试文件 | 测试函数 | 算子 | 说明 |
|----------|----------|------|------|
| `test_matmul.c` | `rknpu_test_matmul` | Matmul INT8 | 31 组数据，全部 byte-match |
| `test_tile_matmul.c` | `rknpu_test_tile_matmul` | Tiled Matmul INT8 | M=256 K=4096 N=32, 4 tiles |
| `test_conv2d.c` | `rknpu_test_conv2d` | Conv2D INT8 | 8x8x32, k3x3, stride1, pad1 |
| `test_dwconv2d.c` | `rknpu_test_dwconv2d` | Depthwise Conv2D INT8 | 8x8x32, k3x3, weight_expand |
| `test_avgpool.c` | `rknpu_test_avgpool` | AvgPool INT8 | pool2x2, stride2, via dwconv |
| `test_maxpool.c` | `rknpu_test_maxpool` | MaxPool INT8 | CPU 实现 (NPU 无此硬件路径) |
| `test_concat.c` | `rknpu_test_concat` | Concat INT8 | Channel 轴拼接, CPU |
| `test_eltwise_add.c` | `rknpu_test_eltwise_add` | Eltwise Add INT8 | 饱和加法, CPU |
| `test_conv2d_bs.c` | `rknpu_test_conv2d_bs` | Conv2D + OUT_CVT | INT32→INT8 硬件 requantize |

## NPU 算子库 (npulib/)

核心实现文件：

| 文件 | 功能 |
|------|------|
| `npu_conv2d.h` | Conv2D 参数结构体 |
| `npu_conv2d.c` | Conv2D/DWConv2D 寄存器生成 |
| `npu_matmul.c` | Matmul 寄存器生成 |
| `npu_dpu.h` | DPU 描述符结构体 (BS/BN/EW/OUT_CVT) |
| `npu_cna.h` | CNA (Convolution Accelerator) 描述符 |
| `npu_hw.h` | 硬件寄存器地址和位域定义 |

## OUT_CVT 算子开发记录

### 目标

在 NPU 硬件内完成 Conv2D INT32 累加器输出到 INT8 的 requantization：

```
output_int8 = saturate_int8((conv_int32 + offset) * scale >> shift)
```

### DPU 流水线

```
Conv INT32 accumulator
  → BS (Bias/Scale)    [bypass=1: 跳过]
  → BN (Batch Norm)    [bypass=0: 数据通过, sub-ops 全部 bypass]
  → EW (Element-wise)  [bypass=1: 跳过]
  → OUT_CVT            [od_bypass=0: 执行 requantize]
  → WDMA               [写 INT8 NC1HWC2 到内存]
```

### 踩坑记录

#### 坑 1: `od_bypass=0` 单独设置无效

**现象**：仅设 `od_bypass=0`（0x4050 bit1），输出仍然是 0x40（截断的 INT32 低字节）。

**原因**：DPU 流水线是串联的。OUT_CVT 位于 BN 之后。如果 BN bypass=1，数据不经过 BN 直接输出到 WDMA，根本到不了 OUT_CVT。

**解决**：必须同时设 `bn_bypass=0`，让数据流经 BN 管道（BN 内部子操作仍可 bypass），这样才能到达 OUT_CVT。

#### 坑 2: `bs_bypass=0` 导致数据损坏

**现象**：设置 `bs_bypass=0, od_bypass=0` 后，OUT_CVT 确实生效了（输出是 INT8 范围值），但数据全部是 ±127/-128 的垃圾。

**原因**：BS 阶段激活时，硬件会从 DPU RDMA 读取 per-channel bias/scale 数据。如果没有正确配置 RDMA 地址和数据，读到的是未初始化的内存垃圾。即使 BS 内的 ALU/MUL 子操作都标记为 bypass，RDMA 仍然会发起读取并将垃圾注入流水线。

**解决**：对于仅需 OUT_CVT（per-layer requantize）的场景，保持 `bs_bypass=1`。改用 `bn_bypass=0` 来打通流水线。

#### 坑 3: INT8 输出格式参数

**NC1HWC2 布局变化**：
- INT32 输出：`size_e=7`, `surf_add = stride * 8`（C2=32 bytes per element * 4B）
- INT8 输出：`size_e=1`, `surf_add = stride * 2`（C2=16 bytes per element * 1B）

**precision 寄存器**：`out_precision=0`（INT8）而非 4（INT32）。

#### 坑 4: ARM char 类型是 unsigned

**现象**：测试中用 `int8_t input = -3`，CPU reference 按 signed 计算得到负数，但 NPU 输入在 ARM 上按 unsigned 解释为 253。

**解决**：避免在测试中依赖负数输入值，改用 offset 参数来测试负输出（如 `offset=-200` 使结果为负）。如果需要负输入，确保声明类型为 `int8_t`（signed）而非 `char`（ARM 默认 unsigned）。

### 最终工作配置

```c
// 关键 bypass 位设置
dpu_desc.bs_bypass     = 1;   // BS 不激活 (避免 RDMA 垃圾)
dpu_desc.bn_bypass     = 0;   // BN 激活 (打通到 OUT_CVT 的通路)
dpu_desc.bn_alu_bypass = 1;   // BN 内部子操作全部 bypass
dpu_desc.bn_mul_bypass = 1;
dpu_desc.bn_relu_bypass = 1;
dpu_desc.od_bypass     = 0;   // 启用 OUT_CVT

// OUT_CVT 参数
dpu_desc.out_cvt_offset = offset;  // int32
dpu_desc.out_cvt_scale  = scale;   // uint16
dpu_desc.out_cvt_shift  = shift;   // uint5

// INT8 输出格式
dpu_desc.size_e_0 = 1;  dpu_desc.size_e_1 = 1;  dpu_desc.size_e_2 = 1;
dpu_desc.surf_add = dst_surf_stride * 2;
// out_precision = 0 (INT8)
```

### 寄存器地址速查

| 寄存器 | 地址 | 作用 |
|--------|------|------|
| DPU_OUT_CVT_OFFSET | 0x4080 | int32 offset |
| DPU_OUT_CVT_SCALE | 0x4084 | uint16 scale (bit16=fp32tofp16) |
| DPU_OUT_CVT_SHIFT | 0x4088 | uint5 shift |
| DPU_BS_OW_CFG | 0x4050 | bit1=od_bypass |
| DPU_BN_CFG | 0x4060 | bn_bypass + BN sub-op bypasses |
| DPU_BS_CFG | 0x4040 | bs_bypass + BS sub-op bypasses |
| DPU_DATA_FORMAT | 0x4010 | out/in/proc precision |

## 项目结构

```
testos-reflector/
├── Makefile              # 编译脚本
├── docs/                 # 文档
├── include/              # 头文件
│   ├── dev/              # 外设驱动头文件
│   ├── lib/              # 库函数头文件
│   ├── mem/              # 内存管理头文件
│   └── t_types.h         # 基础类型定义
├── src/
│   ├── boot/             # 启动代码 (汇编 + 链接脚本)
│   ├── dev/              # 外设驱动 (UART, GIC, Timer)
│   ├── lib/              # 库函数 (printf, string, cache)
│   ├── mem/              # 内存分配器
│   ├── npu/              # NPU 驱动 (寄存器操作, 电源管理)
│   ├── npulib/           # NPU 算子库 (conv2d, matmul 等)
│   ├── t_entry.c         # 内核主入口
│   └── test_*.c          # 各算子测试用例
└── tools/
    ├── orangepi5/        # Orange Pi 5 Plus 烧录工具
    └── rknn/             # librknnrt 参考库 (用于逆向对照)
```

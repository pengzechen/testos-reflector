# Softmax — librknnrt 逆向分析

## 结论

Softmax 在 librknnrt 中是 **CPU fallback 执行**，无 NPU 硬件加速路径。同时存在一个 OpenCL (Mali GPU) 可选路径。

## 证据

### 1. 注册入口 — rknn_register_cpu_ops (0x32C8C8)

`"Softmax"` 是 `rknn_register_cpu_ops` 中**第一个注册的 CPU 算子**（函数的前几条指令就加载此字符串）。Factory 函数为 `rknn_cpu_op_softmax_factory` (0x3E8CF0)。

与 Reshape 不同，Softmax **没有别名**——它单独注册，不与其他 op 共用 factory。

### 2. 实际执行函数 — rknn_cpu_op_softmax (0x3E8D78)

**函数大小**：14080 字节 (0x3700)，是一个非常大的函数。  
**圈复杂度**：221，基本块数：472。

这是一个复杂的多路径函数，包含：
- 多种数据类型的分派逻辑
- NEON SIMD 优化的 float32 softmax 核心
- NC1HWC2 ↔ NCHW 布局转换
- 内存分配和 tensor buffer 管理

#### 数据类型分派

函数入口从 tensor 元数据偏移 `+0x40` 和 `+0x41` 读取输入/输出数据类型，按以下路径分派：

| type 值 | 含义 | 处理方式 |
|---------|------|---------|
| 0x40 (64) | — | 专用路径 |
| 5 | FLOAT16 | 专用路径 |
| 0x10 (16) | — | 专用路径 |
| 0x41 (65) | — | 专用路径 |
| 0xA (10) | INT8 | 调用 sub_35F3A8 进行格式转换后处理 |
| 1 | INT16 | 专用路径 |
| 3 | INT32 | 调用 rknn_tensor_data_format_pack 后处理 |
| 其他 | — | 报错 "Meet unsupported input dtype for softmax" |

不支持的类型会通过 `std::stringstream` 格式化错误信息并返回 -1。

#### INT8 处理路径

对于 INT8 输入（type=10，也就是 TestOS 使用的类型）：

1. **Unpack**: 调用 `rknn_tensor_unpack_nc1hwc2_int8_main` (0x34FF40) 将 NC1HWC2 布局的 INT8 数据解包为 NCHW 连续布局
2. **类型提升**: INT8 → FLOAT32（通过反量化：`float_val = (int8_val - zero_point) * scale`）
3. **Softmax 计算**: 在 FLOAT32 精度下用 NEON SIMD 完成标准 softmax
   - 找 max（用 `float32x4_t` 向量化）
   - 计算 `exp(x - max)` 并求和
   - 归一化 `exp(x - max) / sum`
4. **类型降级**: FLOAT32 → INT8（量化回去）
5. **Pack**: 调用 `rknn_tensor_pack_main` (0x364888) 重新打包为 NC1HWC2 布局

这意味着 librknnrt 的 INT8 softmax **不使用查表法**，而是先转 float、用 NEON 加速的浮点 exp() 计算、再转回 INT8。

### 3. 无特殊快速路径

与 Reshape 不同，`rknn_execute_op_cpu` (0x319250) 中**没有 Softmax 的特殊处理**——走通用 CPU op 调度流程。

### 4. 变体

Softmax 有三个相关变体：

| 函数 | 地址 | 大小 | 说明 |
|------|------|------|------|
| rknn_cpu_op_softmax | 0x3E8D78 | 0x3700 | 基础 Softmax |
| rknn_cpu_op_softmax13 | 0x480500 | 0x1068 | ONNX opset 13 版本，内部调用 rknn_cpu_op_softmax |
| rknn_cpu_op_softmax_mask | 0x484A00 | 0x9F4 | 带 mask 的 Softmax (attention 场景)，内部调用 rknn_cpu_op_softmax |

`exSoftmax13` 和 `exSoftmaxMask` 都是在 `rknn_cpu_op_softmax` 之上的包装器，添加了参数校验和预处理。

### 5. OpenCL GPU 路径（独立于 CPU）

librknnrt 中还包含一个 OpenCL 版本的 Softmax（嵌入的 `.cl` 内核源码），支持三种 softmax 轴：

| 内核函数 | 轴 | 说明 |
|---------|---|------|
| `softmax_channel` | Channel (C) | 对 channel 维做 softmax，按 4 元素为一组处理 |
| `softmax_height` | Height (H) | 对 height 维做 softmax |
| `softmax_width` | Width (W) | 对 width 维做 softmax |

OpenCL 路径通过 `rknn_opencl_pack_nchw_build_kernels` (0xBD900) 加载，在 Mali GPU 上执行。这是一个与 CPU fallback 独立的可选加速路径。

### 6. 相关地址速查

| 符号 | 地址 | 大小 | 说明 |
|------|------|------|------|
| rknn_cpu_op_softmax_factory | 0x3E8CF0 | 0x84 | 工厂函数 |
| rknn_cpu_op_softmax | 0x3E8D78 | 0x3700 | 主执行函数 |
| rknn_cpu_op_softmax13 | 0x480500 | 0x1068 | opset 13 包装器 |
| rknn_cpu_op_softmax_mask | 0x484A00 | 0x9F4 | Mask 包装器 |
| rknn_tensor_unpack_nc1hwc2_int8_main | 0x34FF40 | — | INT8 NC1HWC2 解包 |
| rknn_tensor_pack_main | 0x364888 | — | Tensor 重打包 |
| rknn_opencl_pack_nchw_build_kernels | 0xBD900 | 0x4AC | OpenCL 内核构建 |

## TestOS 实现对比

| 方面 | librknnrt | TestOS |
|------|-----------|--------|
| 精度 | INT8 → FLOAT32 → softmax → INT8 | 纯 INT8 查表法 |
| exp() | NEON SIMD float exp | 256 × uint16_t 查表 (exp_lut) |
| SIMD | float32x4_t 向量化 | 无 (裸机 -mgeneral-regs-only) |
| 布局 | NC1HWC2 ↔ NCHW 转换 | 直接在线性 buffer 操作 |
| 精确度 | 高 (float32 中间精度) | 近似 (定点查表) |

TestOS 使用查表法是因为裸机环境无 FPU/NEON (`-mgeneral-regs-only` CFLAGS)，无法使用浮点指令。librknnrt 运行在 Linux 用户态，可以自由使用 NEON SIMD。

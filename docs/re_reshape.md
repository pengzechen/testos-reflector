# Reshape — librknnrt 逆向分析

## 结论

Reshape 在 librknnrt 中 **100% 是 CPU 执行**，无 NPU 硬件路径。

## 证据

### 1. 注册入口 — rknn_register_cpu_ops (0x32C8C8)

在 `rknn_register_cpu_ops`（~11KB, 注册 76+ CPU 算子）中，以下三个 op 共用同一个 factory 函数 `rknn_cpu_op_reshape_factory` (0x3F3538, 0x84 bytes)：

| Op 名称 | 语义 |
|---------|------|
| Reshape | 维度变换 |
| Squeeze | 移除大小为 1 的维度 |
| Flatten | 展平为 2D |

三者在 rknn 内部被视为**等价操作**，因为它们都不改变数据内容，只改变 shape 元信息。

### 2. 执行层面 — rknn_execute_op_cpu (0x319250)

在通用 CPU op 调度函数中，Reshape 有一个**特殊优化快速路径**：

```
; 比较 op name == "Reshape"
; 检查 input buffer 地址+大小 == output buffer 地址+大小
; 如果相等: B.EQ loc_3198C0 → 直接跳到返回
```

这是**零拷贝优化**：当编译器确认 input 和 output 指向同一块内存时，连 memcpy 都不需要做，纯指针别名。

### 3. 实际执行函数 — rknn_cpu_op_reshape (0x3F35C0)

函数大小：4328 字节 (0x10E8)。当 input/output buffer 不同时，根据数据类型走不同的 `rknn_npu_assign_tensor_buffer_vN` 分支：

| type 值 | 含义 | 调用函数 |
|---------|------|---------|
| 10 | INT8 | rknn_npu_assign_tensor_buffer_v8 |
| 6 | FLOAT32 | rknn_npu_assign_tensor_buffer_v6 |
| 3 | INT32 | rknn_npu_assign_tensor_buffer_v1 (+ 量化参数一致性检查) |
| 5 | FLOAT16 | rknn_npu_assign_tensor_buffer_v3 |
| 1 | INT16 | rknn_npu_assign_tensor_buffer_v2 |
| 9 | — | rknn_npu_assign_tensor_buffer_v1 |
| 65 | — | rknn_npu_assign_tensor_buffer_v4 |
| 7 | INT64 | rknn_npu_assign_tensor_buffer_v7 |

这些 `assign_tensor_buffer` 函数本质就是**内存拷贝**（重新分配输出 tensor 的 buffer 并复制数据）。

### 4. 相关地址速查

| 符号 | 地址 | 大小 | 说明 |
|------|------|------|------|
| rknn_register_cpu_ops | 0x32C8C8 | ~11KB | CPU op 注册总入口 |
| rknn_execute_op_cpu | 0x319250 | — | CPU op 通用调度 |
| rknn_cpu_op_reshape_factory | 0x3F3538 | 0x84 | 工厂函数 (Reshape/Squeeze/Flatten 共用) |
| rknn_cpu_op_reshape | 0x3F35C0 | 0x10E8 | 实际执行函数 |
| rknn_npu_assign_tensor_buffers | 0x3F25F8 | 0xF3C | Tensor buffer 分配/拷贝 |

## TestOS 实现

在 TestOS 中 Reshape 简化为 `memcpy(output, input, total_bytes)`，与 librknnrt 的非零拷贝路径行为一致。对于推理图中的 Reshape 节点，如果上下游 tensor 可复用 buffer，则可进一步优化为零拷贝（跳过 memcpy）。

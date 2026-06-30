# Tiled Matmul 多任务提交探索记录

## 背景

M=256, K=4096, N=32 超过单 tile CBUF 容量（11 banks × 32KB = 352KB），需拆为 4 个 tile（每 tile 64 行），以 `task_number=4` 一次性提交。

## 已确认事实

1. **每个 tile 的 regcmd 数据正确** — 逐个 `task_number=1` 提交全部 PASSED
2. **PC 引擎 step size 正确** — `data_amount=53`，步长 = (53+1)×2 = 108 qwords = gen_matmul_task 输出长度
3. **task_status completed=1** — 硬件只完成 task 0 就停住
4. **NPUOP_pack (librknnrt 0x100900) opcode 映射**：
   - 0x0000-0x0FFF → 0x0101 (OP_REG_PC)
   - 0x1000-0x1FFF → 0x0201 (OP_REG_CNA)
   - 0x3000-0x3FFF → 0x0801 (OP_REG_CORE)
   - 0x4000-0x4FFF → 0x1001 (OP_REG_DPU)
   - 0x5000-0x5FFF → **0x2001**
   - 0x8000-0x8FFF → 0x0041 (OP_40)
   - 0xF000-0xFFFF → 0x0081 (OP_ENABLE)，寄存器硬编码为 0x0008
5. **RK3588 emit_regcmd_tail 序列**（vtable 逆向）：
   - reg 0x5004, value=0x0E (bit1=CNA, bit2=DPU, bit3=PC enable)
   - reg 0x5008, value=0x01 (operation trigger)
   - 这两个寄存器需要 opcode **0x2001**（不是 0x0101）

## 尝试过的方案

| # | 方案 | 结果 | 分析 |
|---|------|------|------|
| 1 | 默认 tail: `OP_ENABLE(0x0D, 0x0008)` | completed=1, task 0 后停住 | OP_ENABLE 写 0x0008 可能是 halt-after-task 语义 |
| 2 | 用 OP_REG_PC(0x0101) 写 0x5004/0x5008 | completed=0 | **opcode 错误**，0x5xxx 需要 0x2001 |
| 3 | 中间 task OP_ENABLE 只设 PC_ENABLE(0x01) | completed=0 | CNA/DPU enable bits 是执行必须的 |
| 4 | PINGPONG 开/关 | task_status bit 变化但不修复 | 与此问题无关 |

## 尚未尝试

- 用正确 opcode 0x2001 写 0x5004=0x0E, 0x5008=0x01 作为 tail
- dump librknnrt 真实多任务 job 的 regcmd buffer 对比
- 检查 CNA_S_POINTER(0x1004) 值是否在 task 间需要交替

## 可行回退方案

多次提交（每个 tile 单独 `task_number=1`）— 已验证可工作，性能略低。

## 关键逆向地址

- `NPUOP_pack`: librknnrt 0x100900
- `emit_regcmd_tail (RK3588)`: vtable[680]=0x1606F0 (写0x5008), vtable[673]=0x1600C0 (写0x5004 bit1), vtable[674]=0x1601C8 (写0x5004 bit2), vtable[675]=0x1602D0 (写0x5004 bit3)
- `rknn_build_npu_task_graph`: 0x2FFAEC（base class tail: 写 0x0014, OP_40, OP_ENABLE(0x18,0x0008)）

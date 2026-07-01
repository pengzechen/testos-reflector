# RK3588 NPU 裸机 INT8 LLaMA 推理 —— 完整实现记录

TestOS 裸机（无 OS / 无 FPU / 无 NEON，`-mgeneral-regs-only`）上，将 `rknpu-llama` 移植为纯整数 INT8 LLaMA 推理，线性层全部走已逆向的 NPU matmul 硬件算子。目标模型 `story`（TinyStories 级）：hidden=128, layers=2, heads=8, kv_heads=4, dqkv=16, intermediate=384, vocab=2048, max_seq=512。

**结果**：RK3588 NPU 上生成连贯英文故事，337 ms/token，能正常输出 `<|end_story|>`。

---

## 一、最终架构：Scaled-INT8（带运行时定点 scale）

**核心结论：纯 INT8 激活不可行**（会坍缩成重复乱码）。每个激活张量必须携带一个运行时定点 scale：

```
real_value = int8_value * scale / 2^20      (Q20 定点，无浮点)
```

数据流（每个 token 每层）：

```
embedding (int8, scale)
  → rms_norm  → (int8, scale)
  → NPU matmul Q/K/V  → INT32 → CPU 动态 requant → (int8, scale)
  → RoPE (int8 原地, scale 不变)
  → attention (CPU): softmax via exp_lut(Q16) → 加权 V → (int8, scale)
  → NPU matmul O  → requant → 残差相加(真实值域) → (int8, scale)
  → rms_norm → NPU gate/up → SwiGLU(sigmoid LUT) → NPU down → 残差 → (int8, scale)
  → 最终 rms_norm → NPU lm_head → INT32 logits
  → 重复惩罚 → argmax → token
```

### 各算子的 scale 处理

| 算子 | 输出 scale 计算 | 说明 |
|------|----------------|------|
| **matmul** | `out_s = max\|acc_int32\| * w_scale * in_scale / (127 * 2^20)` | NPU 出 INT32，CPU 找 max、requant `round(acc*127/max)`、算 scale |
| **rms_norm** | `out_s = max\|rq\| / 127`, `rq = x_i8*w_i8*w_scale/rms_i8` | rms 从 int8 直接算（激活 scale 抵消） |
| **residual add** | `xr = a_i8*a_s + b_i8*b_s`（真实值域），requant | 两操作数按各自 scale 对齐后相加 |
| **SwiGLU** | 见下 | silu(g)*u，需真实值域 |
| **softmax** | Q16 概率 | 见下 |

### 关键子公式

- **matmul requant**（round-half-away-from-zero）：
  ```c
  q = (acc>=0) ? (acc*127*2 + max)/(max*2) : (acc*127*2 - max)/(max*2);
  ```
- **softmax**（exp_lut = `exp(-d/32)*1024`）：
  ```
  score_real = dot(q_i8,k_i8) * q_s * k_s / 2^40 / sqrt(dqkv)
  d = round((max_score - score) * 32)   → clamp[0,255]
  prob_Q16 = exp_lut[d] * 65535 / sum   ← Q16 必须！Q8 会坍缩
  ```
- **SwiGLU sigmoid**（scale-aware LUT，sigmoid_lut = `sigmoid(idx/32)*255-128`）：
  ```
  idx = (g_i8 * g_s * 32) >> 20          → clamp[-128,127]
  sig = (signed char)sigmoid_lut[idx & 0xff] + 128    ← 0..255 = sigmoid*255
  silu = g_i8*g_s * sig / 255            (Q20)
  act  = silu * (u_i8*u_s) >> 20         (Q20)
  ```
- **RoPE**（cos/sin 是 int8 表 `*127`）：`a' = (a*cos - b*sin) >> 7`

---

## 二、所有坑点（按发现顺序）

### 1. dcache flush 方向错误 → NPU 读到全零
DMA buffer 分配顺序是 `dma_input < dma_weights < dma_output < dma_regcmd`（堆向上生长）。
原代码 `clean_dcache_va_range(dma_regcmd, 16MB)` 只 flush 从 regcmd **往上**，完全漏掉了下方的 input/weights/output。NPU 通过 DMA 读到未刷回内存的旧数据（零）。
**修复**：从最低地址 `dma_input` 开始 flush。

### 2. 堆与模型内存重叠 → 权重被覆盖
BSS 约 100MB（test_matmul.c 的静态大数组），堆 `t_mem_init(1<<28)`=256MB 从 `0x68a4000` 延伸到 `0x168a4000`，**吞掉了 TFTP 加载在 0x10000000 的模型**。模型头能解析（在块首），但深处权重被堆分配覆盖。
**修复**：模型 TFTP 地址移到 `0x20000000`（堆之上）。

### 3. 纯 INT8 激活不可行（最重要的架构结论）
用 Python ablation（`tools/ablate.py`）验证了精度阶梯：
- `float` ✓ / `w8`(int8权重+float激活) ✓ / `w8_a8mm`(动态per-tensor int8激活) ✓
- `full_int8_dynamic`(requant成int8无scale) ✗ 坍缩
- `int8_scaled`(int8 + 运行时float scale) ✓

根因：各层激活真实幅度差异巨大，强行归一化到 [-127,127] 而不携带 scale，会丢失层间相对幅度，rms_norm 的整数 rms 也失真。**必须每张量带 scale**。

### 4. OUT_CVT 固定离线 scale 不可行
最初想用 NPU 的 OUT_CVT 硬件（`out = (acc*cvt_scale)>>cvt_shift`）配离线校准的固定 scale。但激活范围每 token 变化，固定 scale 要么饱和到全 127 要么压成全 0。
**修复**：改用 NPU 出 INT32 + CPU 动态 requant（dims≤2048，CPU 开销可忽略），复用已验证的 `gen_matmul_int8`（INT32 输出）。

### 5. softmax 概率精度不足（Q8 → Q16）
softmax 概率用 `exp_lut[d]*255/sum`（Q8）时，注意力权重量化误差累积，生成坍缩成 `mint mint mint`。
**修复**：改用 `*65535/sum`（Q16）。这是最难定位的一个——每次单步 softmax 都"看起来对"，但累积误差在贪心解码下放大。

### 6. `int8_t` 在此工具链是无符号 → SwiGLU 差 5 倍（最后一个 bug）
ARM `char` 默认无符号，本工具链 `int8_t` 也是。读 `sigmoid_lut[idx]` 时漏了 `(signed char)`：`-64` 被读成 `192`，`(192+128)/(64) = 5.0` 倍误差。整个代码库其它地方都有这个 cast，唯独新写的 SwiGLU 漏了。
**修复**：所有 int8 LUT/权重/激活读取一律 `(signed char)`。

### 7. 裸机栈溢出
单个 forward 里有多个 `int64_t[4096]`（各 32KB）栈数组，远超裸机栈。崩溃在 `handle_sync_exception`。
**修复**：全部改 `static`（单线程非重入，放 BSS）。

### 8. LM head logits requant 成 int8 损失精度
最终 argmax 若用 int8 logits，requant 舍入会偏移 argmax。
**修复**：lm_head 保留 INT32 logits 直接 argmax。

### 9. "so so so" 重复
2 层小模型 + 贪心 argmax + INT8 量化 → 概率被压平，argmax 卡在高频词。**不是 bug**。
**修复**：确定性重复惩罚（window=16，最近 token logit 减半），无需随机数。sim 验证甚至能正常生成 `<|end_story|>`。

---

## 三、调试方法论（关键经验）

**不要在硬件上盲试**（每轮 45 秒）。建立**逐位对齐 C 代码的 Python 模拟器**：

1. `tools/ref_forward.py` — 浮点 ground truth，确认模型/权重/tokenizer 本身没问题（出完整故事）
2. `tools/ablate.py` — 精度阶梯 ablation，定位"哪一层量化杀死了信号"
3. `tools/sim_fixedpoint.py` — 纯整数定点模拟器，**是 C 代码的黄金参考，逐行对应**。所有算术（rms/matmul/rope/softmax/sigmoid/residual）都用和 C 相同的整数运算
4. 硬件出问题时，在 C 里 dump 中间张量（`rms_att`、`q`、`attn_out`、`gate`、`act` 等）逐点对照 sim 的对应值，二分定位分歧点

这套方法把 6、8 号这种隐蔽 bug（单步看起来对、累积才崩）快速定位了。

---

## 四、文件清单

### 保留的 Python 工具（`tools/`）
| 文件 | 作用 |
|------|------|
| `quantize_model.py` | safetensors → `.tlm`(权重+Q20 scale+RoPE表) + `.tkn`(BPE) |
| `sim_fixedpoint.py` | **C 代码黄金参考**，纯整数定点模拟器（`--int_ops 1` 出故事） |
| `ref_forward.py` | 浮点 ground truth |
| `ablate.py` | 精度阶梯 ablation（float/w8/w8_a8mm/full_int8/int8_scaled） |

### C 实现（`src/llm/`）
| 文件 | 作用 |
|------|------|
| `llm.h` | 结构体：权重带 `w_scale_q20`，KV cache 带每位置 scale，重复惩罚 ring |
| `llm_forward.c` | 前向（严格对应 sim_fixedpoint.py）+ 重复惩罚采样 |
| `llm_matmul.c` | NPU INT8 matmul → INT32 → CPU 动态 requant + scale |
| `llm_model.c` | 解析 .tlm，分配 buffer |
| `llm_tokenizer.c` | BPE 编解码，`▁`(U+2581)→空格 |
| `llm_ops.c` | gather / argmax（rms/rope 现内联在 forward） |

### 复用的已验证算子
`npu_matmul.c` (`gen_matmul_int8` INT32 输出) / `rknpu.c` (任务提交) / `npu_math.c` (`exp_lut`, `sigmoid_lut`)。

---

## 五、模型文件格式

### `.tlm`（magic "TLM1"）
```
header: magic(4) + config[10×u32](40) + num_tensors(4)
  config = {hidden, intermediate, n_heads, n_layers, n_kv_heads,
            vocab, max_seq, bos_id, eos_id, tie_embed}
per-tensor entry (28B): tensor_id(u16) layer_idx(u16) rows(u32) cols(u32)
                        data_offset(u32) data_size(u32) w_scale_q20(u32) reserved(u32)
RoPE tables: cos_int8[max_seq][dqkv/2] + sin_int8[...]  (各 *127)
tensor data: INT8 权重紧密排列
```
权重量化：per-tensor 对称，`scale = max|w|/127`，存为 Q20 `round(scale*2^20)`。

### TFTP 加载地址
```
dtb    0x300000
model  0x20000000   (.tlm, 堆之上)
token  0x20200000   (.tkn)
kernel 0x400000
```

---

## 六、复现命令

```bash
# 量化
python3 tools/quantize_model.py --model_dir <safetensors_dir> --output_dir . --name story

# 模拟器验证（应出连贯故事）
python3 tools/sim_fixedpoint.py --model_dir <dir> --n 50 --int_ops 1

# 编译
wsl bash -ic "cd .../testos-reflector && make && make uimg"
```

U-Boot：
```
tftp 0x20000000 story.tlm; tftp 0x20200000 story.tkn; tftp 0x400000 kernel.uimg; bootm 0x400000 - 0x300000
```

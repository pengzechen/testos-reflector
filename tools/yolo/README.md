# YOLOv5n INT8 推理工具链

本目录实现了 YOLOv5n 320x320 从 PyTorch 到纯整数 INT8 推理的完整流水线，用于 TestOS bare-metal RK3588 NPU 推理。

## 模型来源

- **YOLOv5n** 来自 [ultralytics/yolov5](https://github.com/ultralytics/yolov5)，最小版本（~1.9M 参数）
- 原始权重 `yolov5n.pt` 从 https://github.com/ultralytics/yolov5/releases 下载
- 测试图片 `bus.jpg` 为 ultralytics 提供的标准 COCO 测试图

## 文件说明

```
tools/yolo/
├── ref_forward.py       # FP32 参考前向（NumPy 实现，与 PyTorch cosine=1.0）
├── quantize.py          # BN 融合 + INT8 对称量化 → .tyolo 二进制 + .npz
├── sim_fixedpoint.py    # 纯整数 INT8 定点模拟器（直接映射到 C 实现）
├── weights/
│   ├── yolov5n.pt       # PyTorch 原始权重（3.9MB）
│   ├── yolov5n.tyolo    # INT8 量化二进制，可直接烧录到目标板（1.8MB）
│   ├── yolov5n_int8.npz # INT8 权重 + FP32 权重，供模拟器使用
│   └── bus.jpg          # 测试图片
└── README.md
```

## 量化策略

与 LLaMA 一致，使用 **per-tensor 对称量化 + 动态 requant**：

- 所有 BN 融合到 Conv 权重：`W_fused = W * gamma / sqrt(var + eps)`
- 权重 per-tensor 对称量化为 INT8：`scale = absmax / 127`
- 中间激活用 `(int8_data, scale_q20)` 表示：`real = int8 * scale_q20 / 2^20`
- 每层动态 requant：计算 absmax → 缩放到 [-127, 127] → 传递新的 scale_q20
- SiLU 通过 `sigmoid_lut[256]` 查表实现，零浮点

## 环境准备

需要 Python 3 + NumPy + OpenCV + PyTorch（仅量化脚本需要 PyTorch）。

```bash
python3 -m venv /tmp/yolo_env
source /tmp/yolo_env/bin/activate
pip install numpy opencv-python torch torchvision

# YOLOv5 源码（加载 .pt 权重需要）
git clone https://github.com/ultralytics/yolov5 /tmp/yolov5
```

## 运行

所有脚本均在 `tools/yolo/` 目录下运行：

```bash
cd tools/yolo
source /tmp/yolo_env/bin/activate
```

### 1. FP32 参考前向

验证 NumPy 实现与 PyTorch 完全一致（cosine=1.0）：

```bash
python ref_forward.py --weights weights/yolov5n.pt --image weights/bus.jpg
```

输出示例：
```
Detections (original image coordinates):
  [0] person: conf=0.859, box=[39.9, 395.0, 220.8, 897.2]
  [1] person: conf=0.694, box=[215.5, 397.0, 360.7, 848.7]
  [2] person: conf=0.509, box=[685.0, 370.8, 810.0, 864.4]
  [3] bus: conf=0.833, box=[23.2, 227.6, 810.0, 781.5]
```

### 2. 量化导出

将 PyTorch 权重量化为 INT8 并导出 `.tyolo` 二进制：

```bash
python quantize.py --weights weights/yolov5n.pt \
                   --output weights/yolov5n.tyolo \
                   --npz weights/yolov5n_int8.npz
```

输出 87 个原子算子（conv, maxpool, upsample, concat, add），60 个量化 conv 层。

### 3. INT8 定点模拟器

纯整数前向传播，验证 INT8 流水线的检测精度：

```bash
python sim_fixedpoint.py --npz weights/yolov5n_int8.npz --image weights/bus.jpg
```

输出示例：
```
INT8 Detections:
  [0] person: conf=0.967 [76,429,194,840]
  [1] person: conf=0.830 [697,325,767,879]
  [2] person: conf=0.827 [245,420,336,822]
  [3] bus: conf=0.876 [65,298,709,763]

FP32 vs INT8 detect output comparison:
  Det0: cosine=0.9897
  Det1: cosine=0.9915
  Det2: cosine=0.9948
```

## 网络结构（87 个原子算子）

```
Input 320x320x3
  ├─ Backbone
  │   Conv(6x6,s2) → 16ch → Conv(3x3,s2) → 32ch → C3(n=1)
  │   → Conv(s2) → 64ch → C3(n=2) ──────────────── P3 (40x40)
  │   → Conv(s2) → 128ch → C3(n=3) ─────────────── P4 (20x20)
  │   → Conv(s2) → 256ch → C3(n=1) → SPPF ──────── P5 (10x10)
  ├─ Neck (FPN + PAN)
  │   Conv1x1 → Upsample 2x → Concat(P4) → C3
  │   Conv1x1 → Upsample 2x → Concat(P3) → C3 ─── 小目标 (40x40)
  │   Conv(s2) → Concat → C3 ─────────────────────── 中目标 (20x20)
  │   Conv(s2) → Concat → C3 ─────────────────────── 大目标 (10x10)
  └─ Detect Head
      3x Conv1x1 → [3 anchors × (5 + 80 classes)] per scale
```

## 已有 NPU 算子映射

| YOLO 算子 | TestOS 实现 | 位置 |
|-----------|------------|------|
| Conv2D (fused BN) | NPU 硬件加速 | `npu_conv2d.c` |
| SiLU (sigmoid × x) | sigmoid_lut + mul CPU | `npu_math.c` |
| MaxPool 5×5 | CPU | `npu_pool.c` |
| Concat (channel) | CPU | `npu_concat.c` |
| Eltwise Add (residual) | CPU | `npu_eltwise.c` |
| Upsample 2x | 需新增（像素复制，~20 行） | — |

## .tyolo 二进制格式

```
Offset  Size   Description
0x00    4      Magic: 0x4F4C5954 ("TYOL")
0x04    4      Version (1)
0x08    4      nc (80)
0x0C    4      n_ops (87)
0x10    4      n_detect (3)
0x14    4      img_size (320)
0x18    8      reserved
0x20    6      detect_indices (3 × uint16)
0x26    72     anchors (3×3×2 × float32)
0x6E    12     strides (3 × float32)
0x7A    2088   op_table (87 × 24 bytes, 6 × uint32 per op)
...     ...    weight_data (int8 weights + fp32 bias + q20 scales)
```

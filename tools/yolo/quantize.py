#!/usr/bin/env python3
"""
Quantize YOLOv5n to INT8 and produce .tyolo binary for TestOS bare-metal inference.

Loads PyTorch weights, fuses BN into Conv, does per-tensor symmetric INT8 quantization,
exports .tyolo binary with network topology + weights.

Usage:
  python yolo_quantize.py --weights /tmp/yolo_weights/yolov5n.pt --output yolov5n.tyolo

.tyolo format:
  Header (magic + config)
  Layer table (per-layer type, params, weight offsets)
  Weight data (int8 weights + int32 bias + scales)
"""

import argparse
import struct
import sys
import numpy as np
from pathlib import Path

# ═══════════════════════════════════════════════════════════
#  Constants
# ═══════════════════════════════════════════════════════════

TYOLO_MAGIC = 0x4F4C5954  # "TYOL"
SCALE_SHIFT = 20

# Layer types
LAYER_CONV_SILU  = 0   # Conv + BN(fused) + SiLU
LAYER_CONV_LIN   = 1   # Conv only (detect head, no activation)
LAYER_C3         = 2   # C3 module (encoded as sequence of convs + add)
LAYER_SPPF       = 3   # SPPF
LAYER_UPSAMPLE   = 4   # Nearest 2x upsample
LAYER_CONCAT     = 5   # Channel concat
LAYER_MAXPOOL    = 6   # MaxPool

# Since the topology is fixed for YOLOv5n, we flatten the entire network
# into a sequence of primitive ops (conv, maxpool, upsample, concat, add)
# to make the C implementation straightforward.

# Primitive op types (for the flattened graph)
OP_CONV       = 0   # conv + bias + silu
OP_CONV_LIN   = 1   # conv + bias (no activation)
OP_MAXPOOL    = 2   # maxpool 5x5 s1 p2
OP_UPSAMPLE   = 3   # nearest 2x
OP_CONCAT     = 4   # channel concat (2 inputs)
OP_ADD        = 5   # element-wise add (residual)


def quantize_symmetric(tensor_f32):
    """Per-tensor symmetric quantization: float -> int8 + scale."""
    abs_max = np.max(np.abs(tensor_f32))
    if abs_max < 1e-10:
        return np.zeros_like(tensor_f32, dtype=np.int8), 1e-10
    scale = abs_max / 127.0
    quantized = np.clip(np.round(tensor_f32 / scale), -128, 127).astype(np.int8)
    return quantized, scale


def to_q20(scale_float):
    """Convert float scale to Q20 fixed point."""
    return max(1, int(round(scale_float * (1 << SCALE_SHIFT))))


# ═══════════════════════════════════════════════════════════
#  Load & fuse
# ═══════════════════════════════════════════════════════════

def load_and_fuse(pt_path):
    """Load YOLOv5n, fuse BN, return FP32 weights dict."""
    sys.path.insert(0, '/tmp/yolov5')
    import torch

    ckpt = torch.load(pt_path, map_location='cpu', weights_only=False)
    model = ckpt['model'].float().eval()
    sd = model.state_dict()

    detect = model.model[-1]
    anchors = detect.anchors.numpy()
    strides = detect.stride.numpy()
    nc = detect.nc

    layers = {}
    for key in sorted(sd.keys()):
        if 'conv.weight' not in key:
            continue
        prefix = key.replace('.conv.weight', '')
        conv_w = sd[key].numpy()

        bn_w_key = prefix + '.bn.weight'
        if bn_w_key in sd:
            gamma = sd[bn_w_key].numpy()
            beta = sd[prefix + '.bn.bias'].numpy()
            mean = sd[prefix + '.bn.running_mean'].numpy()
            var = sd[prefix + '.bn.running_var'].numpy()
            std = np.sqrt(var + 0.001)
            scale = gamma / std
            fused_w = conv_w * scale.reshape(-1, 1, 1, 1)
            fused_b = -mean * scale + beta
            layers[prefix] = {'weight': fused_w, 'bias': fused_b}
        else:
            bias_key = prefix + '.bias'
            bias = sd[bias_key].numpy() if bias_key in sd else np.zeros(conv_w.shape[0])
            layers[prefix] = {'weight': conv_w, 'bias': bias}

    for i in range(3):
        key_w = f'model.24.m.{i}.weight'
        key_b = f'model.24.m.{i}.bias'
        if key_w in sd:
            layers[f'model.24.m.{i}'] = {
                'weight': sd[key_w].numpy(), 'bias': sd[key_b].numpy()
            }

    return layers, anchors, strides, nc


# ═══════════════════════════════════════════════════════════
#  Flatten YOLOv5n into primitive ops
# ═══════════════════════════════════════════════════════════

def build_op_graph(layers):
    """Build a flat list of ops describing the full YOLOv5n forward pass.

    Each op: {
        'type': OP_*,
        'input': int or list of ints (index into results array, -1=network input),
        'output': int (result index),
        'conv_name': str (for conv ops),
        'stride': int (for conv ops),
        'padding': int (for conv ops),
    }

    We assign result indices: the network input is -1.
    Each op produces result[op_index].
    """
    ops = []
    idx = 0

    def add_conv(input_idx, name, stride=1, padding=None, linear=False):
        nonlocal idx
        w = layers[name]['weight']
        kh = w.shape[2]
        if padding is None:
            padding = 2 if kh == 6 else kh // 2
        ops.append({
            'type': OP_CONV_LIN if linear else OP_CONV,
            'input': input_idx,
            'output': idx,
            'conv_name': name,
            'stride': stride,
            'padding': padding,
        })
        result = idx
        idx += 1
        return result

    def add_maxpool(input_idx):
        nonlocal idx
        ops.append({
            'type': OP_MAXPOOL,
            'input': input_idx,
            'output': idx,
        })
        result = idx
        idx += 1
        return result

    def add_upsample(input_idx):
        nonlocal idx
        ops.append({
            'type': OP_UPSAMPLE,
            'input': input_idx,
            'output': idx,
        })
        result = idx
        idx += 1
        return result

    def add_concat(input_a, input_b):
        nonlocal idx
        ops.append({
            'type': OP_CONCAT,
            'input': [input_a, input_b],
            'output': idx,
        })
        result = idx
        idx += 1
        return result

    def add_add(input_a, input_b):
        nonlocal idx
        ops.append({
            'type': OP_ADD,
            'input': [input_a, input_b],
            'output': idx,
        })
        result = idx
        idx += 1
        return result

    def add_bottleneck(input_idx, prefix, shortcut=True):
        cv1 = add_conv(input_idx, f'{prefix}.cv1')
        cv2 = add_conv(cv1, f'{prefix}.cv2')
        if shortcut:
            return add_add(cv2, input_idx)
        return cv2

    def add_c3(input_idx, prefix, n_bottlenecks, shortcut=True):
        a = add_conv(input_idx, f'{prefix}.cv1')
        b = add_conv(input_idx, f'{prefix}.cv2')
        for i in range(n_bottlenecks):
            a = add_bottleneck(a, f'{prefix}.m.{i}', shortcut=shortcut)
        cat = add_concat(a, b)
        out = add_conv(cat, f'{prefix}.cv3')
        return out

    def add_sppf(input_idx, prefix):
        x = add_conv(input_idx, f'{prefix}.cv1')
        m1 = add_maxpool(x)
        m2 = add_maxpool(m1)
        m3 = add_maxpool(m2)
        # concat x,m1,m2,m3 — we chain: concat(x,m1) -> concat(result,m2) -> concat(result,m3)
        c1 = add_concat(x, m1)
        c2 = add_concat(c1, m2)
        c3 = add_concat(c2, m3)
        out = add_conv(c3, f'{prefix}.cv2')
        return out

    # Network input is -1
    net_in = -1

    # Backbone
    x0 = add_conv(net_in, 'model.0', stride=2, padding=2)   # [16,160,160]
    x1 = add_conv(x0, 'model.1', stride=2)                   # [32,80,80]
    x2 = add_c3(x1, 'model.2', 1)                            # [32,80,80]
    x3 = add_conv(x2, 'model.3', stride=2)                   # [64,40,40]
    x4 = add_c3(x3, 'model.4', 2)                            # [64,40,40]
    x5 = add_conv(x4, 'model.5', stride=2)                   # [128,20,20]
    x6 = add_c3(x5, 'model.6', 3)                            # [128,20,20]
    x7 = add_conv(x6, 'model.7', stride=2)                   # [256,10,10]
    x8 = add_c3(x7, 'model.8', 1)                            # [256,10,10]
    x9 = add_sppf(x8, 'model.9')                             # [256,10,10]

    # Neck
    x10 = add_conv(x9, 'model.10')                           # [128,10,10]
    x11 = add_upsample(x10)                                  # [128,20,20]
    x12 = add_concat(x11, x6)                                # [256,20,20]
    x13 = add_c3(x12, 'model.13', 1, shortcut=False)         # [128,20,20]
    x14 = add_conv(x13, 'model.14')                          # [64,20,20]
    x15 = add_upsample(x14)                                  # [64,40,40]
    x16 = add_concat(x15, x4)                                # [128,40,40]
    x17 = add_c3(x16, 'model.17', 1, shortcut=False)         # [64,40,40]

    x18 = add_conv(x17, 'model.18', stride=2)                # [64,20,20]
    x19 = add_concat(x18, x14)                               # [128,20,20]
    x20 = add_c3(x19, 'model.20', 1, shortcut=False)         # [128,20,20]

    x21 = add_conv(x20, 'model.21', stride=2)                # [128,10,10]
    x22 = add_concat(x21, x10)                               # [256,10,10]
    x23 = add_c3(x22, 'model.23', 1, shortcut=False)         # [256,10,10]

    # Detect head (linear conv, no activation)
    det0 = add_conv(x17, 'model.24.m.0', linear=True)        # [255,40,40]
    det1 = add_conv(x20, 'model.24.m.1', linear=True)        # [255,20,20]
    det2 = add_conv(x23, 'model.24.m.2', linear=True)        # [255,10,10]

    return ops, [det0, det1, det2]


# ═══════════════════════════════════════════════════════════
#  Quantize & export .tyolo
# ═══════════════════════════════════════════════════════════

def build_tyolo(layers, anchors, strides, nc, output_path):
    """Quantize all conv weights to INT8, export .tyolo binary."""

    ops, detect_indices = build_op_graph(layers)
    print(f"  Flattened graph: {len(ops)} ops, detect at indices {detect_indices}")

    # Quantize all conv weights
    quant_weights = {}  # name -> (w_int8, w_scale, b_fp32)
    for op in ops:
        if op['type'] not in (OP_CONV, OP_CONV_LIN):
            continue
        name = op['conv_name']
        if name in quant_weights:
            continue
        w = layers[name]['weight']
        b = layers[name]['bias']
        w_int8, w_scale = quantize_symmetric(w)
        quant_weights[name] = (w_int8, w_scale, b)

    # === Build binary ===

    # Header: 32 bytes
    # magic(4) + version(4) + nc(4) + n_ops(4) + n_detect(4) + img_size(4)
    # + anchors_offset(4) + reserved(4)
    n_ops = len(ops)
    n_detect = 3

    # Op table: each op is fixed size
    # We use a simple format: 6 x uint32 = 24 bytes
    #   [0] type(8) | stride(8) | padding(8) | flags(8)
    #   [1] input_a(16) | input_b(16) signed
    #   [2] output(16) | out_c(16)
    #   [3] in_c(16) | kh(8) | kw(8)
    #   [4] weight_offset(32)
    #   [5] bias_offset(32)
    OP_ENTRY_SIZE = 24

    header_size = 32
    detect_table_size = n_detect * 2  # 3 x uint16 indices
    anchors_size = 3 * 3 * 2 * 4  # 3 scales * 3 anchors * 2(w,h) * float32
    strides_size = 3 * 4  # 3 x float32
    op_table_size = n_ops * OP_ENTRY_SIZE

    data_offset_base = header_size + detect_table_size + anchors_size + strides_size + op_table_size
    # Align to 16 bytes
    data_offset_base = (data_offset_base + 15) & ~15

    # Pack weight data
    weight_data = bytearray()
    weight_offsets = {}  # name -> (offset, w_size, b_offset, b_size)

    for name in sorted(quant_weights.keys()):
        w_int8, w_scale, b_fp32 = quant_weights[name]
        w_bytes = w_int8.tobytes()

        # Bias: store as int32 (quantized) + float scale, OR store as fp32 for now.
        # For C code we'll need int32 bias. bias_int32 = round(bias_fp32 / (in_scale * w_scale))
        # But in_scale is dynamic. So store bias as fp32 and w_scale as fp32.
        # Actually: store w_scale as Q20, and bias as fp32 bytes.
        # The C code will pre-compute bias_int32 at runtime or we calibrate.
        #
        # Simpler: store w_int8, w_scale_q20, bias_fp32. The sim_fixedpoint.py
        # will handle the bias conversion dynamically.
        b_bytes = b_fp32.astype(np.float32).tobytes()
        scale_bytes = struct.pack('<I', to_q20(w_scale))

        # Layout: [scale_q20(4)] [w_int8 data] [bias_fp32 data]
        offset = len(weight_data)
        weight_data.extend(scale_bytes)
        w_data_offset = len(weight_data)
        weight_data.extend(w_bytes)
        b_data_offset = len(weight_data)
        weight_data.extend(b_bytes)

        weight_offsets[name] = {
            'base_offset': data_offset_base + offset,
            'scale_offset': data_offset_base + offset,
            'w_offset': data_offset_base + w_data_offset,
            'w_size': len(w_bytes),
            'b_offset': data_offset_base + b_data_offset,
            'b_size': len(b_bytes),
            'w_scale': w_scale,
            'shape': list(w_int8.shape),
        }

    total_size = data_offset_base + len(weight_data)

    # Build the file
    buf = bytearray()

    # Header
    buf.extend(struct.pack('<I', TYOLO_MAGIC))
    buf.extend(struct.pack('<I', 1))  # version
    buf.extend(struct.pack('<I', nc))
    buf.extend(struct.pack('<I', n_ops))
    buf.extend(struct.pack('<I', n_detect))
    buf.extend(struct.pack('<I', 320))  # img_size
    buf.extend(struct.pack('<I', 0))    # reserved
    buf.extend(struct.pack('<I', 0))    # reserved

    # Detect indices
    for di in detect_indices:
        buf.extend(struct.pack('<H', di))

    # Anchors (float32)
    for s in range(3):
        for a in range(3):
            buf.extend(struct.pack('<ff', float(anchors[s, a, 0]), float(anchors[s, a, 1])))

    # Strides (float32)
    for s in range(3):
        buf.extend(struct.pack('<f', float(strides[s])))

    # Op table
    def pack_op(op_type, stride, padding, flags, in_a, in_b, out, out_c, in_c, kh, kw, w_off, b_off):
        word0 = (op_type & 0xFF) | ((stride & 0xFF) << 8) | ((padding & 0xFF) << 16) | ((flags & 0xFF) << 24)
        word1 = (in_a & 0xFFFF) | ((in_b & 0xFFFF) << 16)
        word2 = (out & 0xFFFF) | ((out_c & 0xFFFF) << 16)
        word3 = (in_c & 0xFFFF) | ((kh & 0xFF) << 16) | ((kw & 0xFF) << 24)
        return struct.pack('<IIIIII', word0, word1, word2, word3, w_off, b_off)

    for op in ops:
        op_type = op['type']
        if op_type in (OP_CONV, OP_CONV_LIN):
            name = op['conv_name']
            wo = weight_offsets[name]
            w_shape = wo['shape']
            input_a = op['input'] if isinstance(op['input'], int) else op['input'][0]
            entry = pack_op(
                op_type, op['stride'], op['padding'], 0,
                input_a & 0xFFFF, 0xFFFF, op['output'],
                w_shape[0], w_shape[1], w_shape[2], w_shape[3],
                wo['w_offset'], wo['b_offset'])
        elif op_type == OP_MAXPOOL:
            entry = pack_op(
                op_type, 1, 2, 0,
                op['input'] & 0xFFFF, 0xFFFF, op['output'],
                0, 0, 5, 5, 0, 0)
        elif op_type == OP_UPSAMPLE:
            entry = pack_op(
                op_type, 0, 0, 0,
                op['input'] & 0xFFFF, 0xFFFF, op['output'],
                0, 0, 0, 0, 0, 0)
        elif op_type in (OP_CONCAT, OP_ADD):
            inputs = op['input']
            entry = pack_op(
                op_type, 0, 0, 0,
                inputs[0] & 0xFFFF, inputs[1] & 0xFFFF, op['output'],
                0, 0, 0, 0, 0, 0)
        else:
            entry = b'\x00' * OP_ENTRY_SIZE

        assert len(entry) == OP_ENTRY_SIZE, f"Entry size {len(entry)} != {OP_ENTRY_SIZE}"
        buf.extend(entry)

    # Pad to data_offset_base
    while len(buf) < data_offset_base:
        buf.extend(b'\x00')

    # Weight data
    buf.extend(weight_data)

    with open(output_path, 'wb') as f:
        f.write(buf)

    print(f"\nWritten {output_path}: {len(buf):,} bytes")
    print(f"  {n_ops} ops, {len(quant_weights)} quantized conv layers")
    print(f"  Weight data: {len(weight_data):,} bytes at offset {data_offset_base}")

    # Print per-layer quantization stats
    print(f"\nQuantization summary:")
    for name in sorted(quant_weights.keys()):
        w_int8, w_scale, b_fp32 = quant_weights[name]
        wo = weight_offsets[name]
        print(f"  {name}: {wo['shape']}, scale={w_scale:.6f}, scale_q20={to_q20(w_scale)}")

    return ops, detect_indices, quant_weights, weight_offsets


# ═══════════════════════════════════════════════════════════
#  Also export a .npz for the fixedpoint simulator
# ═══════════════════════════════════════════════════════════

def export_npz(layers, quant_weights, anchors, strides, nc, output_path):
    """Export all weights as .npz for easy loading by sim_fixedpoint.py."""
    save_dict = {
        'anchors': anchors,
        'strides': strides,
        'nc': np.array([nc]),
    }

    for name, (w_int8, w_scale, b_fp32) in quant_weights.items():
        safe_name = name.replace('.', '_')
        save_dict[f'{safe_name}_w_int8'] = w_int8
        save_dict[f'{safe_name}_w_scale'] = np.array([w_scale])
        save_dict[f'{safe_name}_bias'] = b_fp32

    # Also save FP32 weights for reference comparison
    for name, data in layers.items():
        safe_name = name.replace('.', '_')
        save_dict[f'{safe_name}_w_fp32'] = data['weight']

    np.savez(output_path, **save_dict)
    print(f"\nWritten {output_path}: {Path(output_path).stat().st_size:,} bytes")


def main():
    ap = argparse.ArgumentParser(description='Quantize YOLOv5n to INT8 .tyolo')
    ap.add_argument('--weights', default='weights/yolov5n.pt')
    ap.add_argument('--output', default='weights/yolov5n.tyolo')
    ap.add_argument('--npz', default='weights/yolov5n_int8.npz')
    args = ap.parse_args()

    print("Loading and fusing BN...")
    layers, anchors, strides, nc = load_and_fuse(args.weights)
    print(f"  {len(layers)} fused conv layers, nc={nc}")

    print("\nBuilding .tyolo binary...")
    ops, det_idx, qw, wo = build_tyolo(layers, anchors, strides, nc, args.output)

    print("\nExporting .npz for simulator...")
    export_npz(layers, qw, anchors, strides, nc, args.npz)

    print("\nDone!")


if __name__ == '__main__':
    main()

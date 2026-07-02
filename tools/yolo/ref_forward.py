#!/usr/bin/env python3
"""
YOLOv5n FP32 reference forward — NumPy-only implementation.
Loads PyTorch weights, fuses BN, runs FP32 inference, verifies detection.

Usage:
  python yolo_ref_forward.py --weights /tmp/yolo_weights/yolov5n.pt \
                             --image /tmp/yolo_weights/bus.jpg \
                             --size 320
"""

import argparse
import sys
import struct
import json
import numpy as np
from pathlib import Path


# ═══════════════════════════════════════════════════════════
#  Weight loading & BN fusion
# ═══════════════════════════════════════════════════════════

def load_yolov5n_weights(pt_path):
    """Load YOLOv5n .pt file, fuse BN into Conv, return dict of numpy arrays."""
    sys.path.insert(0, str(Path(pt_path).parent.parent / 'yolov5'))
    if '/tmp/yolov5' not in sys.path:
        sys.path.insert(0, '/tmp/yolov5')
    import torch

    ckpt = torch.load(pt_path, map_location='cpu', weights_only=False)
    model = ckpt['model'].float().eval()
    sd = model.state_dict()

    # Extract anchors and strides
    detect = model.model[-1]
    anchors = detect.anchors.numpy()    # [3, 3, 2]
    strides = detect.stride.numpy()     # [3]
    nc = detect.nc

    # Fuse BN into Conv weights
    layers = {}
    processed = set()

    for key in sorted(sd.keys()):
        if 'conv.weight' in key:
            prefix = key.replace('.conv.weight', '')
            bn_prefix = prefix + '.bn'
            conv_w = sd[key].numpy()  # [out_c, in_c, kh, kw]

            # Check if BN exists
            bn_w_key = bn_prefix + '.weight'
            if bn_w_key in sd:
                gamma = sd[bn_w_key].numpy()
                beta = sd[bn_prefix + '.bias'].numpy()
                mean = sd[bn_prefix + '.running_mean'].numpy()
                var = sd[bn_prefix + '.running_var'].numpy()
                eps = 0.001  # YOLOv5 default

                # Fuse: W_fused = W * gamma / sqrt(var + eps)
                #        b_fused = (0 - mean) * gamma / sqrt(var + eps) + beta
                std = np.sqrt(var + eps)
                scale = gamma / std

                fused_w = conv_w * scale.reshape(-1, 1, 1, 1)
                fused_b = -mean * scale + beta

                layers[prefix] = {'weight': fused_w, 'bias': fused_b}
            else:
                # No BN (detect head convs have bias directly)
                bias_key = prefix + '.bias'
                bias = sd[bias_key].numpy() if bias_key in sd else np.zeros(conv_w.shape[0])
                layers[prefix] = {'weight': conv_w, 'bias': bias}

            processed.add(prefix)

    # Detect head convs: model.24.m.{0,1,2}
    for i in range(3):
        key_w = f'model.24.m.{i}.weight'
        key_b = f'model.24.m.{i}.bias'
        if key_w in sd:
            layers[f'model.24.m.{i}'] = {
                'weight': sd[key_w].numpy(),
                'bias': sd[key_b].numpy()
            }

    return layers, anchors, strides, nc


# ═══════════════════════════════════════════════════════════
#  NumPy operators
# ═══════════════════════════════════════════════════════════

def conv2d(x, weight, bias, stride=1, padding=0):
    """Conv2D: x is [C,H,W], weight is [out_c, in_c, kh, kw]."""
    if isinstance(stride, int):
        stride = (stride, stride)
    if isinstance(padding, int):
        padding = (padding, padding)

    C, H, W = x.shape
    out_c, in_c, kh, kw = weight.shape
    ph, pw = padding
    sh, sw = stride

    if ph > 0 or pw > 0:
        x = np.pad(x, ((0, 0), (ph, ph), (pw, pw)), mode='constant')
    _, Hp, Wp = x.shape

    oh = (Hp - kh) // sh + 1
    ow = (Wp - kw) // sw + 1

    out = np.zeros((out_c, oh, ow), dtype=np.float32)
    for oc in range(out_c):
        for i in range(oh):
            for j in range(ow):
                patch = x[:, i*sh:i*sh+kh, j*sw:j*sw+kw]
                out[oc, i, j] = np.sum(patch * weight[oc]) + bias[oc]
    return out


def conv2d_fast(x, weight, bias, stride=1, padding=0):
    """Conv2D using im2col for speed. x is [C,H,W]."""
    if isinstance(stride, int):
        stride = (stride, stride)
    if isinstance(padding, int):
        padding = (padding, padding)

    C, H, W = x.shape
    out_c, in_c, kh, kw = weight.shape
    ph, pw = padding
    sh, sw = stride

    if ph > 0 or pw > 0:
        x = np.pad(x, ((0, 0), (ph, ph), (pw, pw)), mode='constant')
    _, Hp, Wp = x.shape

    oh = (Hp - kh) // sh + 1
    ow = (Wp - kw) // sw + 1

    # im2col
    cols = np.zeros((in_c * kh * kw, oh * ow), dtype=np.float32)
    col = 0
    for i in range(oh):
        for j in range(ow):
            patch = x[:, i*sh:i*sh+kh, j*sw:j*sw+kw]  # [in_c, kh, kw]
            cols[:, col] = patch.reshape(-1)
            col += 1

    # matmul: [out_c, in_c*kh*kw] @ [in_c*kh*kw, oh*ow] -> [out_c, oh*ow]
    w_flat = weight.reshape(out_c, -1)
    out_flat = w_flat @ cols + bias.reshape(-1, 1)

    return out_flat.reshape(out_c, oh, ow)


def silu(x):
    return x * (1.0 / (1.0 + np.exp(-np.clip(x, -20, 20))))


def maxpool2d(x, kernel_size=5, stride=1, padding=2):
    C, H, W = x.shape
    Hp = H + 2 * padding
    Wp = W + 2 * padding
    if padding > 0:
        xp = np.full((C, Hp, Wp), -np.inf, dtype=x.dtype)
        xp[:, padding:padding+H, padding:padding+W] = x
    else:
        xp = x

    oh = (Hp - kernel_size) // stride + 1
    ow = (Wp - kernel_size) // stride + 1
    out = np.zeros((C, oh, ow), dtype=x.dtype)
    for i in range(oh):
        for j in range(ow):
            out[:, i, j] = xp[:, i*stride:i*stride+kernel_size, j*stride:j*stride+kernel_size].reshape(C, -1).max(axis=1)
    return out


def upsample_nearest_2x(x):
    C, H, W = x.shape
    out = np.zeros((C, H * 2, W * 2), dtype=x.dtype)
    for i in range(H):
        for j in range(W):
            out[:, i*2, j*2] = x[:, i, j]
            out[:, i*2, j*2+1] = x[:, i, j]
            out[:, i*2+1, j*2] = x[:, i, j]
            out[:, i*2+1, j*2+1] = x[:, i, j]
    return out


def concat_channel(tensors):
    return np.concatenate(tensors, axis=0)


# ═══════════════════════════════════════════════════════════
#  YOLOv5n building blocks
# ═══════════════════════════════════════════════════════════

class ConvBnSiLU:
    def __init__(self, weight, bias):
        self.w = weight
        self.b = bias

    def __call__(self, x):
        # Infer padding from kernel size
        kh, kw = self.w.shape[2], self.w.shape[3]
        if kh == 6:
            pad = 2
        elif kh == 3:
            pad = 1
        elif kh == 1:
            pad = 0
        else:
            pad = kh // 2
        # Infer stride: if name suggests stride=2 it's embedded in the caller
        return x  # placeholder — caller provides stride


class YOLOv5nForward:
    def __init__(self, layers, anchors, strides, nc):
        self.L = layers
        self.anchors = anchors  # [3, 3, 2] in grid units
        self.strides = strides  # [8, 16, 32]
        self.nc = nc

    def conv_silu(self, x, prefix, stride=1, padding=None):
        w = self.L[prefix]['weight']
        b = self.L[prefix]['bias']
        kh = w.shape[2]
        if padding is None:
            if kh == 6:
                padding = 2
            else:
                padding = kh // 2
        y = conv2d_fast(x, w, b, stride=stride, padding=padding)
        return silu(y)

    def conv_linear(self, x, prefix):
        """Conv without activation (detect head)."""
        w = self.L[prefix]['weight']
        b = self.L[prefix]['bias']
        return conv2d_fast(x, w, b, stride=1, padding=0)

    def bottleneck(self, x, prefix, shortcut=True):
        """C3 Bottleneck: 1x1 -> 3x3, with optional residual."""
        h = self.conv_silu(x, f'{prefix}.cv1')
        h = self.conv_silu(h, f'{prefix}.cv2')
        if shortcut and h.shape == x.shape:
            h = h + x
        return h

    def c3(self, x, prefix, n_bottlenecks=1, shortcut=True):
        """C3 module: cv1 branch through bottlenecks, cv2 branch direct, concat + cv3."""
        a = self.conv_silu(x, f'{prefix}.cv1')
        b = self.conv_silu(x, f'{prefix}.cv2')

        for i in range(n_bottlenecks):
            a = self.bottleneck(a, f'{prefix}.m.{i}', shortcut=shortcut)

        out = concat_channel([a, b])
        out = self.conv_silu(out, f'{prefix}.cv3')
        return out

    def sppf(self, x, prefix):
        """SPPF: cv1 -> 3x maxpool5x5 -> concat(x, m1, m2, m3) -> cv2."""
        x = self.conv_silu(x, f'{prefix}.cv1')
        m1 = maxpool2d(x, kernel_size=5, stride=1, padding=2)
        m2 = maxpool2d(m1, kernel_size=5, stride=1, padding=2)
        m3 = maxpool2d(m2, kernel_size=5, stride=1, padding=2)
        out = concat_channel([x, m1, m2, m3])
        out = self.conv_silu(out, f'{prefix}.cv2')
        return out

    def forward(self, x):
        """Full YOLOv5n forward. x: [3, H, W] float32, 0-1 range.
        Returns raw detect outputs: list of [255, h, w] for 3 scales."""

        # Backbone
        x0 = self.conv_silu(x, 'model.0', stride=2, padding=2)       # [16, 160, 160]
        print(f"  Layer  0 Conv:    {x0.shape}")
        x1 = self.conv_silu(x0, 'model.1', stride=2)                  # [32, 80, 80]
        print(f"  Layer  1 Conv:    {x1.shape}")
        x2 = self.c3(x1, 'model.2', n_bottlenecks=1)                  # [32, 80, 80]
        print(f"  Layer  2 C3:      {x2.shape}")
        x3 = self.conv_silu(x2, 'model.3', stride=2)                  # [64, 40, 40]
        print(f"  Layer  3 Conv:    {x3.shape}")
        x4 = self.c3(x3, 'model.4', n_bottlenecks=2)                  # [64, 40, 40]
        print(f"  Layer  4 C3:      {x4.shape}")
        x5 = self.conv_silu(x4, 'model.5', stride=2)                  # [128, 20, 20]
        print(f"  Layer  5 Conv:    {x5.shape}")
        x6 = self.c3(x5, 'model.6', n_bottlenecks=3)                  # [128, 20, 20]
        print(f"  Layer  6 C3:      {x6.shape}")
        x7 = self.conv_silu(x6, 'model.7', stride=2)                  # [256, 10, 10]
        print(f"  Layer  7 Conv:    {x7.shape}")
        x8 = self.c3(x7, 'model.8', n_bottlenecks=1)                  # [256, 10, 10]
        print(f"  Layer  8 C3:      {x8.shape}")
        x9 = self.sppf(x8, 'model.9')                                 # [256, 10, 10]
        print(f"  Layer  9 SPPF:    {x9.shape}")

        # Neck (FPN + PAN)
        x10 = self.conv_silu(x9, 'model.10')                          # [128, 10, 10]
        print(f"  Layer 10 Conv:    {x10.shape}")
        x11 = upsample_nearest_2x(x10)                                # [128, 20, 20]
        print(f"  Layer 11 Upsample:{x11.shape}")
        x12 = concat_channel([x11, x6])                               # [256, 20, 20]
        print(f"  Layer 12 Concat:  {x12.shape}")
        x13 = self.c3(x12, 'model.13', n_bottlenecks=1, shortcut=False)  # [128, 20, 20]
        print(f"  Layer 13 C3:      {x13.shape}")
        x14 = self.conv_silu(x13, 'model.14')                         # [64, 20, 20]
        print(f"  Layer 14 Conv:    {x14.shape}")
        x15 = upsample_nearest_2x(x14)                                # [64, 40, 40]
        print(f"  Layer 15 Upsample:{x15.shape}")
        x16 = concat_channel([x15, x4])                               # [128, 40, 40]
        print(f"  Layer 16 Concat:  {x16.shape}")
        x17 = self.c3(x16, 'model.17', n_bottlenecks=1, shortcut=False)  # [64, 40, 40]
        print(f"  Layer 17 C3:      {x17.shape}")

        x18 = self.conv_silu(x17, 'model.18', stride=2)               # [64, 20, 20]
        print(f"  Layer 18 Conv:    {x18.shape}")
        x19 = concat_channel([x18, x14])                              # [128, 20, 20]
        print(f"  Layer 19 Concat:  {x19.shape}")
        x20 = self.c3(x19, 'model.20', n_bottlenecks=1, shortcut=False)  # [128, 20, 20]
        print(f"  Layer 20 C3:      {x20.shape}")

        x21 = self.conv_silu(x20, 'model.21', stride=2)               # [128, 10, 10]
        print(f"  Layer 21 Conv:    {x21.shape}")
        x22 = concat_channel([x21, x10])                              # [256, 10, 10]
        print(f"  Layer 22 Concat:  {x22.shape}")
        x23 = self.c3(x22, 'model.23', n_bottlenecks=1, shortcut=False)  # [256, 10, 10]
        print(f"  Layer 23 C3:      {x23.shape}")

        # Detect head (1x1 conv, no activation)
        det0 = self.conv_linear(x17, 'model.24.m.0')  # [255, 40, 40]
        det1 = self.conv_linear(x20, 'model.24.m.1')  # [255, 20, 20]
        det2 = self.conv_linear(x23, 'model.24.m.2')  # [255, 10, 10]

        print(f"  Detect 0: {det0.shape}")
        print(f"  Detect 1: {det1.shape}")
        print(f"  Detect 2: {det2.shape}")

        return [det0, det1, det2]


# ═══════════════════════════════════════════════════════════
#  Post-processing: decode + NMS
# ═══════════════════════════════════════════════════════════

def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-np.clip(x, -20, 20)))


def decode_detections(raw_outputs, anchors, strides, nc, conf_thresh=0.25):
    """Decode raw [255, h, w] outputs to boxes.
    Returns list of [x1, y1, x2, y2, conf, class_id]."""
    na = 3  # anchors per scale
    no = 5 + nc  # outputs per anchor

    all_boxes = []

    for scale_idx, (raw, stride) in enumerate(zip(raw_outputs, strides)):
        _, gh, gw = raw.shape
        # Reshape: [255, h, w] -> [3, 85, h, w]
        pred = raw.reshape(na, no, gh, gw)

        for a in range(na):
            anchor_w = anchors[scale_idx, a, 0]  # in grid units
            anchor_h = anchors[scale_idx, a, 1]

            for gy in range(gh):
                for gx in range(gw):
                    obj_logit = pred[a, 4, gy, gx]
                    obj_conf = sigmoid(obj_logit)
                    if obj_conf < conf_thresh:
                        continue

                    # Decode box
                    tx = sigmoid(pred[a, 0, gy, gx])
                    ty = sigmoid(pred[a, 1, gy, gx])
                    tw = pred[a, 2, gy, gx]
                    th = pred[a, 3, gy, gx]

                    cx = (tx * 2.0 - 0.5 + gx) * stride
                    cy = (ty * 2.0 - 0.5 + gy) * stride
                    w = (sigmoid(tw) * 2.0) ** 2 * anchor_w * stride
                    h = (sigmoid(th) * 2.0) ** 2 * anchor_h * stride

                    # Class scores
                    cls_logits = pred[a, 5:, gy, gx]
                    cls_probs = sigmoid(cls_logits)
                    cls_id = np.argmax(cls_probs)
                    cls_conf = cls_probs[cls_id]

                    score = obj_conf * cls_conf
                    if score < conf_thresh:
                        continue

                    x1 = cx - w / 2
                    y1 = cy - h / 2
                    x2 = cx + w / 2
                    y2 = cy + h / 2

                    all_boxes.append([x1, y1, x2, y2, score, cls_id])

    return np.array(all_boxes) if all_boxes else np.zeros((0, 6))


def nms(boxes, iou_thresh=0.45):
    """Non-maximum suppression. boxes: [N, 6] (x1,y1,x2,y2,score,class)."""
    if len(boxes) == 0:
        return boxes

    result = []
    classes = np.unique(boxes[:, 5])

    for cls in classes:
        mask = boxes[:, 5] == cls
        cls_boxes = boxes[mask]

        # Sort by score descending
        order = np.argsort(-cls_boxes[:, 4])
        cls_boxes = cls_boxes[order]

        keep = []
        while len(cls_boxes) > 0:
            keep.append(cls_boxes[0])
            if len(cls_boxes) == 1:
                break

            # IoU with rest
            x1 = np.maximum(cls_boxes[0, 0], cls_boxes[1:, 0])
            y1 = np.maximum(cls_boxes[0, 1], cls_boxes[1:, 1])
            x2 = np.minimum(cls_boxes[0, 2], cls_boxes[1:, 2])
            y2 = np.minimum(cls_boxes[0, 3], cls_boxes[1:, 3])
            inter = np.maximum(0, x2 - x1) * np.maximum(0, y2 - y1)

            area0 = (cls_boxes[0, 2] - cls_boxes[0, 0]) * (cls_boxes[0, 3] - cls_boxes[0, 1])
            areas = (cls_boxes[1:, 2] - cls_boxes[1:, 0]) * (cls_boxes[1:, 3] - cls_boxes[1:, 1])
            iou = inter / (area0 + areas - inter + 1e-6)

            remaining = np.where(iou <= iou_thresh)[0]
            cls_boxes = cls_boxes[remaining + 1]

        result.extend(keep)

    return np.array(result) if result else np.zeros((0, 6))


COCO_NAMES = [
    'person', 'bicycle', 'car', 'motorcycle', 'airplane', 'bus', 'train', 'truck',
    'boat', 'traffic light', 'fire hydrant', 'stop sign', 'parking meter', 'bench',
    'bird', 'cat', 'dog', 'horse', 'sheep', 'cow', 'elephant', 'bear', 'zebra',
    'giraffe', 'backpack', 'umbrella', 'handbag', 'tie', 'suitcase', 'frisbee',
    'skis', 'snowboard', 'sports ball', 'kite', 'baseball bat', 'baseball glove',
    'skateboard', 'surfboard', 'tennis racket', 'bottle', 'wine glass', 'cup',
    'fork', 'knife', 'spoon', 'bowl', 'banana', 'apple', 'sandwich', 'orange',
    'broccoli', 'carrot', 'hot dog', 'pizza', 'donut', 'cake', 'chair', 'couch',
    'potted plant', 'bed', 'dining table', 'toilet', 'tv', 'laptop', 'mouse',
    'remote', 'keyboard', 'cell phone', 'microwave', 'oven', 'toaster', 'sink',
    'refrigerator', 'book', 'clock', 'vase', 'scissors', 'teddy bear',
    'hair drier', 'toothbrush'
]


# ═══════════════════════════════════════════════════════════
#  Image preprocessing
# ═══════════════════════════════════════════════════════════

def preprocess_image(image_path, size=320):
    """Load image, resize to (size, size), normalize to [0, 1].
    Returns: (input_tensor [3,H,W], original_shape, scale_info)."""
    import cv2
    img = cv2.imread(image_path)
    if img is None:
        raise FileNotFoundError(f"Cannot load image: {image_path}")
    orig_h, orig_w = img.shape[:2]

    # Letterbox resize
    scale = min(size / orig_h, size / orig_w)
    new_h, new_w = int(orig_h * scale), int(orig_w * scale)
    resized = cv2.resize(img, (new_w, new_h))

    # Pad to target size
    padded = np.full((size, size, 3), 114, dtype=np.uint8)
    pad_h = (size - new_h) // 2
    pad_w = (size - new_w) // 2
    padded[pad_h:pad_h+new_h, pad_w:pad_w+new_w] = resized

    # BGR -> RGB, HWC -> CHW, normalize
    x = padded[:, :, ::-1].astype(np.float32) / 255.0
    x = x.transpose(2, 0, 1)  # [3, H, W]

    return x, (orig_h, orig_w), (scale, pad_h, pad_w)


def rescale_boxes(boxes, img_size, orig_shape, scale_info):
    """Rescale boxes from padded coordinates back to original image."""
    if len(boxes) == 0:
        return boxes
    scale, pad_h, pad_w = scale_info
    boxes = boxes.copy()
    boxes[:, 0] = (boxes[:, 0] - pad_w) / scale
    boxes[:, 1] = (boxes[:, 1] - pad_h) / scale
    boxes[:, 2] = (boxes[:, 2] - pad_w) / scale
    boxes[:, 3] = (boxes[:, 3] - pad_h) / scale
    # Clip to original image
    boxes[:, 0] = np.clip(boxes[:, 0], 0, orig_shape[1])
    boxes[:, 1] = np.clip(boxes[:, 1], 0, orig_shape[0])
    boxes[:, 2] = np.clip(boxes[:, 2], 0, orig_shape[1])
    boxes[:, 3] = np.clip(boxes[:, 3], 0, orig_shape[0])
    return boxes


# ═══════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════

def main():
    ap = argparse.ArgumentParser(description='YOLOv5n FP32 reference forward')
    ap.add_argument('--weights', default='weights/yolov5n.pt')
    ap.add_argument('--image', default='weights/bus.jpg')
    ap.add_argument('--size', type=int, default=320)
    ap.add_argument('--conf', type=float, default=0.25)
    ap.add_argument('--iou', type=float, default=0.45)
    args = ap.parse_args()

    print("Loading YOLOv5n weights...")
    layers, anchors, strides, nc = load_yolov5n_weights(args.weights)
    print(f"  Loaded {len(layers)} fused conv layers, nc={nc}")
    print(f"  Anchors: {anchors.shape}, Strides: {strides}")

    # Print layer summary
    total_params = 0
    for name in sorted(layers.keys()):
        w = layers[name]['weight']
        b = layers[name]['bias']
        n = w.size + b.size
        total_params += n
        # print(f"  {name}: weight={list(w.shape)}, bias={list(b.shape)}, params={n}")
    print(f"  Total parameters: {total_params:,}")

    print(f"\nPreprocessing image: {args.image} -> {args.size}x{args.size}")
    x, orig_shape, scale_info = preprocess_image(args.image, args.size)
    print(f"  Input tensor: {x.shape}, range=[{x.min():.3f}, {x.max():.3f}]")

    print(f"\nRunning FP32 forward pass...")
    net = YOLOv5nForward(layers, anchors, strides, nc)
    raw_outputs = net.forward(x)

    print(f"\nDecoding detections (conf_thresh={args.conf})...")
    boxes = decode_detections(raw_outputs, anchors, strides, nc, conf_thresh=args.conf)
    print(f"  Pre-NMS: {len(boxes)} detections")

    boxes = nms(boxes, iou_thresh=args.iou)
    print(f"  Post-NMS: {len(boxes)} detections")

    if len(boxes) > 0:
        boxes = rescale_boxes(boxes, args.size, orig_shape, scale_info)
        print(f"\nDetections (original image coordinates):")
        for i, box in enumerate(boxes):
            x1, y1, x2, y2, conf, cls = box
            cls_name = COCO_NAMES[int(cls)] if int(cls) < len(COCO_NAMES) else f"class_{int(cls)}"
            print(f"  [{i}] {cls_name}: conf={conf:.3f}, box=[{x1:.1f}, {y1:.1f}, {x2:.1f}, {y2:.1f}]")
    else:
        print("\nNo detections found.")

    # Compare with PyTorch for validation
    print("\n=== Comparing with PyTorch reference ===")
    try:
        import torch
        sys.path.insert(0, '/tmp/yolov5')
        ckpt = torch.load(args.weights, map_location='cpu', weights_only=False)
        pt_model = ckpt['model'].float().eval()
        pt_model.model[-1].inplace = False

        import cv2
        img_t = torch.from_numpy(x).unsqueeze(0)
        with torch.no_grad():
            pt_out = pt_model(img_t)

        # Compare raw detect outputs layer by layer
        pt_save = {}
        y = img_t
        for i, m in enumerate(pt_model.model):
            mtype = m.__class__.__name__
            if mtype == 'Concat':
                y = m([y, pt_save[m.f[1]]])
            elif mtype == 'Detect':
                for j in range(m.nl):
                    pt_raw = m.m[j](pt_save[m.f[j]])
                    np_raw = raw_outputs[j]
                    pt_np = pt_raw.squeeze(0).numpy()
                    diff = np.abs(pt_np - np_raw)
                    cos = np.sum(pt_np * np_raw) / (np.linalg.norm(pt_np) * np.linalg.norm(np_raw) + 1e-10)
                    print(f"  Detect {j}: max_diff={diff.max():.6f}, mean_diff={diff.mean():.6f}, cosine={cos:.6f}")
                break
            else:
                f = m.f if hasattr(m, 'f') else -1
                if isinstance(f, list):
                    y = m([pt_save[ff] for ff in f])
                elif f != -1:
                    y = m(pt_save[f])
                else:
                    y = m(y)
            pt_save[i] = y

    except Exception as e:
        print(f"  PyTorch comparison skipped: {e}")


if __name__ == '__main__':
    main()

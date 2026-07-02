#!/usr/bin/env python3
"""
YOLOv5n INT8 fixed-point simulator.

Every activation between layers is (int8[C,H,W], scale_q20):
  real_value = int8_value * scale_q20 / 2^20

All arithmetic uses Python int / numpy int64 — models what the C code
does on the bare-metal RK3588 target (-mgeneral-regs-only, no FPU).

Usage:
  python yolo_sim_fixedpoint.py --npz /tmp/yolo_weights/yolov5n_int8.npz \
                                --image /tmp/yolo_weights/bus.jpg
"""

import argparse, sys, re
import numpy as np
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from ref_forward import (preprocess_image, decode_detections,
                         nms, rescale_boxes, COCO_NAMES)

SHIFT = 20


def to_q20(f):
    return max(1, int(round(f * (1 << SHIFT))))


def load_luts(path):
    txt = open(path).read()
    def grab(name):
        m = re.search(name + r'\[256\]\s*=\s*\{(.+?)\};', txt, re.S)
        return np.array([int(x) for x in re.findall(r'-?\d+', m.group(1))][:256],
                        dtype=np.int64)
    return grab('sigmoid_lut'), grab('exp_lut')


class YOLOv5nInt8:

    def __init__(self, npz_path, math_c='src/npulib/npu_math.c'):
        d = np.load(npz_path, allow_pickle=True)
        self.sigmoid_lut, _ = load_luts(math_c)
        self.anchors = d['anchors']
        self.strides = d['strides']
        self.nc = int(d['nc'][0])

        self.W = {}
        for key in d.files:
            if not key.endswith('_w_int8'):
                continue
            safe = key[:-7]
            name = safe.replace('_', '.')
            w_i8 = d[f'{safe}_w_int8'].astype(np.int64)
            w_scale_f = float(d[f'{safe}_w_scale'][0])
            bias_f = d[f'{safe}_bias'].astype(np.float64)
            ws_q20 = to_q20(w_scale_f)
            # bias_q = round(bias_fp / w_scale * 2^SHIFT)
            # At runtime: bias_acc = bias_q // input_scale_q20
            if w_scale_f > 1e-12:
                bias_q = np.round(bias_f / w_scale_f * (1 << SHIFT)).astype(np.int64)
            else:
                bias_q = np.zeros(len(bias_f), dtype=np.int64)
            self.W[name] = {'w': w_i8, 'ws': ws_q20, 'bias_q': bias_q}

    # ── primitive ops ──────────────────────────────────────

    def _im2col_matmul(self, x_i8, name, stride, padding):
        """Conv2D via im2col + int64 matmul. Returns acc[oc, oh, ow]."""
        w = self.W[name]['w']
        oc, ic, kh, kw = w.shape
        C, H, Wd = x_i8.shape

        if padding > 0:
            xp = np.zeros((C, H + 2 * padding, Wd + 2 * padding), dtype=np.int64)
            xp[:, padding:padding + H, padding:padding + Wd] = x_i8
        else:
            xp = x_i8
        _, Hp, Wp = xp.shape
        oh = (Hp - kh) // stride + 1
        ow = (Wp - kw) // stride + 1

        cols = np.zeros((ic * kh * kw, oh * ow), dtype=np.int64)
        for i in range(oh):
            for j in range(ow):
                cols[:, i * ow + j] = xp[:, i*stride:i*stride+kh,
                                          j*stride:j*stride+kw].reshape(-1)
        acc = w.reshape(oc, -1) @ cols
        return acc.reshape(oc, oh, ow)

    def _add_bias(self, acc, name, x_s):
        b = self.W[name]['bias_q'] // x_s
        return acc + b.reshape(-1, 1, 1)

    @staticmethod
    def _requant(acc):
        """Requant int64 → int8. Returns (int8_array, max_abs)."""
        mx = int(np.max(np.abs(acc)))
        if mx < 1:
            mx = 1
        oi8 = np.clip(
            (acc * 127 * 2 + np.sign(acc) * mx) // (mx * 2),
            -128, 127).astype(np.int64)
        return oi8, mx

    def conv_silu(self, x_i8, x_s, name, stride=1, padding=None):
        """Conv + bias + SiLU, all in INT8."""
        kh = self.W[name]['w'].shape[2]
        if padding is None:
            padding = 2 if kh == 6 else kh // 2
        ws = self.W[name]['ws']

        acc = self._im2col_matmul(x_i8, name, stride, padding)
        acc = self._add_bias(acc, name, x_s)

        # Requant conv output to int8 for sigmoid LUT
        y_i8, y_mx = self._requant(acc)
        y_s = max(1, (y_mx * ws * x_s) // (127 * (1 << SHIFT)))

        # SiLU = x * sigmoid(x) via LUT
        # sigmoid index: real_value * 32, clipped to [-128, 127]
        idx = np.clip((y_i8 * y_s * 32) >> SHIFT, -128, 127)
        sig = self.sigmoid_lut[(idx & 0xFF).astype(np.intp)] + 128  # [0..255]

        # silu_acc = y_i8 * sig, range [-128*255, 127*255]
        silu_acc = y_i8 * sig
        out_i8, silu_mx = self._requant(silu_acc)
        # real = y_i8 * y_s / 2^SHIFT * sig / 255
        # out_s = silu_mx * y_s / (127 * 255)
        out_s = max(1, (silu_mx * y_s) // (127 * 255))
        return out_i8, out_s

    def conv_linear(self, x_i8, x_s, name):
        """Conv + bias, no activation."""
        ws = self.W[name]['ws']
        acc = self._im2col_matmul(x_i8, name, 1, 0)
        acc = self._add_bias(acc, name, x_s)
        out_i8, mx = self._requant(acc)
        out_s = max(1, (mx * ws * x_s) // (127 * (1 << SHIFT)))
        return out_i8, out_s

    def maxpool(self, x_i8, x_s, ksize=5, stride=1, padding=2):
        """MaxPool on int8 values. Scale unchanged."""
        C, H, W = x_i8.shape
        if padding > 0:
            xp = np.full((C, H + 2 * padding, W + 2 * padding), -128, dtype=np.int64)
            xp[:, padding:padding + H, padding:padding + W] = x_i8
        else:
            xp = x_i8
        _, Hp, Wp = xp.shape
        oh = (Hp - ksize) // stride + 1
        ow = (Wp - ksize) // stride + 1
        out = np.full((C, oh, ow), -128, dtype=np.int64)
        for i in range(oh):
            for j in range(ow):
                out[:, i, j] = xp[:, i*stride:i*stride+ksize,
                                   j*stride:j*stride+ksize].reshape(C, -1).max(axis=1)
        return out, x_s

    def upsample_2x(self, x_i8, x_s):
        """Nearest-neighbor 2x upsample. Scale unchanged."""
        return np.repeat(np.repeat(x_i8, 2, axis=1), 2, axis=2), x_s

    def concat(self, a_i8, a_s, b_i8, b_s):
        """Channel concat with scale alignment to larger scale."""
        if a_s >= b_s:
            b_r = np.clip((b_i8 * b_s + a_s // 2) // a_s, -128, 127).astype(np.int64)
            return np.concatenate([a_i8, b_r], axis=0), a_s
        else:
            a_r = np.clip((a_i8 * a_s + b_s // 2) // b_s, -128, 127).astype(np.int64)
            return np.concatenate([a_r, b_i8], axis=0), b_s

    def add_res(self, a_i8, a_s, b_i8, b_s):
        """Element-wise add (residual) with scale alignment + requant."""
        result = a_i8 * a_s + b_i8 * b_s
        oi8, mx = self._requant(result)
        return oi8, max(1, mx // 127)

    # ── building blocks ────────────────────────────────────

    def bottleneck(self, x_i8, x_s, prefix, shortcut=True):
        h, hs = self.conv_silu(x_i8, x_s, f'{prefix}.cv1')
        h, hs = self.conv_silu(h, hs, f'{prefix}.cv2')
        if shortcut:
            h, hs = self.add_res(h, hs, x_i8, x_s)
        return h, hs

    def c3(self, x_i8, x_s, prefix, n=1, shortcut=True):
        a, a_s = self.conv_silu(x_i8, x_s, f'{prefix}.cv1')
        b, b_s = self.conv_silu(x_i8, x_s, f'{prefix}.cv2')
        for i in range(n):
            a, a_s = self.bottleneck(a, a_s, f'{prefix}.m.{i}', shortcut=shortcut)
        cat, cat_s = self.concat(a, a_s, b, b_s)
        return self.conv_silu(cat, cat_s, f'{prefix}.cv3')

    def sppf(self, x_i8, x_s, prefix):
        x, xs = self.conv_silu(x_i8, x_s, f'{prefix}.cv1')
        m1, m1s = self.maxpool(x, xs)
        m2, m2s = self.maxpool(m1, m1s)
        m3, m3s = self.maxpool(m2, m2s)
        c1, c1s = self.concat(x, xs, m1, m1s)
        c2, c2s = self.concat(c1, c1s, m2, m2s)
        c3, c3s = self.concat(c2, c2s, m3, m3s)
        return self.conv_silu(c3, c3s, f'{prefix}.cv2')

    # ── full forward ───────────────────────────────────────

    def forward(self, x_fp32):
        """Full YOLOv5n INT8 forward.
        x_fp32: [3, 320, 320] float32 in [0, 1].
        Returns list of 3 float32 raw detect tensors for post-processing.
        """
        # Quantize input
        mx = float(np.max(np.abs(x_fp32)))
        if mx < 1e-10:
            mx = 1e-10
        in_s_f = mx / 127.0
        x_i8 = np.clip(np.round(x_fp32 / in_s_f), -128, 127).astype(np.int64)
        x_s = to_q20(in_s_f)

        def p(tag, t, s):
            print(f"  {tag:20s} {str(t.shape):16s} s_q20={s:8d} "
                  f"[{int(t.min()):4d},{int(t.max()):4d}]")

        p('input', x_i8, x_s)

        # Backbone
        x0, s0 = self.conv_silu(x_i8, x_s, 'model.0', stride=2, padding=2)
        p('Conv0', x0, s0)
        x1, s1 = self.conv_silu(x0, s0, 'model.1', stride=2)
        p('Conv1', x1, s1)
        x2, s2 = self.c3(x1, s1, 'model.2', n=1)
        p('C3_2', x2, s2)
        x3, s3 = self.conv_silu(x2, s2, 'model.3', stride=2)
        p('Conv3', x3, s3)
        x4, s4 = self.c3(x3, s3, 'model.4', n=2)
        p('C3_4', x4, s4)
        x5, s5 = self.conv_silu(x4, s4, 'model.5', stride=2)
        p('Conv5', x5, s5)
        x6, s6 = self.c3(x5, s5, 'model.6', n=3)
        p('C3_6', x6, s6)
        x7, s7 = self.conv_silu(x6, s6, 'model.7', stride=2)
        p('Conv7', x7, s7)
        x8, s8 = self.c3(x7, s7, 'model.8', n=1)
        p('C3_8', x8, s8)
        x9, s9 = self.sppf(x8, s8, 'model.9')
        p('SPPF9', x9, s9)

        # Neck (FPN + PAN)
        x10, s10 = self.conv_silu(x9, s9, 'model.10')
        p('Conv10', x10, s10)
        x11, s11 = self.upsample_2x(x10, s10)
        p('Up11', x11, s11)
        x12, s12 = self.concat(x11, s11, x6, s6)
        p('Cat12', x12, s12)
        x13, s13 = self.c3(x12, s12, 'model.13', n=1, shortcut=False)
        p('C3_13', x13, s13)
        x14, s14 = self.conv_silu(x13, s13, 'model.14')
        p('Conv14', x14, s14)
        x15, s15 = self.upsample_2x(x14, s14)
        p('Up15', x15, s15)
        x16, s16 = self.concat(x15, s15, x4, s4)
        p('Cat16', x16, s16)
        x17, s17 = self.c3(x16, s16, 'model.17', n=1, shortcut=False)
        p('C3_17', x17, s17)

        x18, s18 = self.conv_silu(x17, s17, 'model.18', stride=2)
        p('Conv18', x18, s18)
        x19, s19 = self.concat(x18, s18, x14, s14)
        p('Cat19', x19, s19)
        x20, s20 = self.c3(x19, s19, 'model.20', n=1, shortcut=False)
        p('C3_20', x20, s20)

        x21, s21 = self.conv_silu(x20, s20, 'model.21', stride=2)
        p('Conv21', x21, s21)
        x22, s22 = self.concat(x21, s21, x10, s10)
        p('Cat22', x22, s22)
        x23, s23 = self.c3(x22, s22, 'model.23', n=1, shortcut=False)
        p('C3_23', x23, s23)

        # Detect heads (linear conv, no activation)
        d0, ds0 = self.conv_linear(x17, s17, 'model.24.m.0')
        p('Det0', d0, ds0)
        d1, ds1 = self.conv_linear(x20, s20, 'model.24.m.1')
        p('Det1', d1, ds1)
        d2, ds2 = self.conv_linear(x23, s23, 'model.24.m.2')
        p('Det2', d2, ds2)

        # Convert int8 detect outputs to real for post-processing
        det_real = []
        for di8, ds in [(d0, ds0), (d1, ds1), (d2, ds2)]:
            real = di8.astype(np.float64) * ds / (1 << SHIFT)
            det_real.append(real.astype(np.float32))
        return det_real


def main():
    ap = argparse.ArgumentParser(description='YOLOv5n INT8 fixed-point simulator')
    ap.add_argument('--npz', default='weights/yolov5n_int8.npz')
    ap.add_argument('--image', default='weights/bus.jpg')
    ap.add_argument('--size', type=int, default=320)
    ap.add_argument('--conf', type=float, default=0.25)
    ap.add_argument('--iou', type=float, default=0.45)
    ap.add_argument('--math_c', default=str(Path(__file__).resolve().parent.parent.parent / 'src/npulib/npu_math.c'))
    args = ap.parse_args()

    print("Loading INT8 model...")
    model = YOLOv5nInt8(args.npz, args.math_c)
    print(f"  {len(model.W)} conv layers, nc={model.nc}")

    print(f"\nPreprocessing: {args.image} -> {args.size}x{args.size}")
    x, orig_shape, scale_info = preprocess_image(args.image, args.size)
    print(f"  Input: {x.shape}, range=[{x.min():.3f}, {x.max():.3f}]")

    print(f"\nINT8 forward pass:")
    raw = model.forward(x)

    print(f"\nDecoding detections (conf={args.conf})...")
    boxes = decode_detections(raw, model.anchors, model.strides, model.nc, args.conf)
    print(f"  Pre-NMS: {len(boxes)}")
    boxes = nms(boxes, args.iou)
    print(f"  Post-NMS: {len(boxes)}")

    if len(boxes) > 0:
        boxes = rescale_boxes(boxes, args.size, orig_shape, scale_info)
        print(f"\nINT8 Detections:")
        for i, box in enumerate(boxes):
            x1, y1, x2, y2, conf, cls = box
            name = COCO_NAMES[int(cls)] if int(cls) < len(COCO_NAMES) else f"cls_{int(cls)}"
            print(f"  [{i}] {name}: conf={conf:.3f} [{x1:.0f},{y1:.0f},{x2:.0f},{y2:.0f}]")
    else:
        print("\nNo detections.")

    # Compare with FP32 reference if available
    print("\n=== FP32 vs INT8 detect output comparison ===")
    try:
        from ref_forward import load_yolov5n_weights, YOLOv5nForward
        layers, anchors, strides, nc = load_yolov5n_weights('weights/yolov5n.pt')
        ref = YOLOv5nForward(layers, anchors, strides, nc)
        ref_raw = ref.forward(x)
        for i in range(3):
            fp = ref_raw[i].flatten()
            iq = raw[i].flatten()
            cos = np.dot(fp, iq) / (np.linalg.norm(fp) * np.linalg.norm(iq) + 1e-10)
            mse = np.mean((fp - iq) ** 2)
            print(f"  Det{i}: cosine={cos:.4f}, MSE={mse:.4f}, "
                  f"fp_range=[{fp.min():.2f},{fp.max():.2f}], "
                  f"i8_range=[{iq.min():.2f},{iq.max():.2f}]")
    except Exception as e:
        print(f"  Skipped: {e}")


if __name__ == '__main__':
    main()

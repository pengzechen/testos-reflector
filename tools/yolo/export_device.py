#!/usr/bin/env python3
"""
export_device.py — produce an INTEGER-ONLY YOLOv5n bundle (.ydev) for the
bare-metal RK3588 target, and a bit-exact Python "C-model" to validate the
device pipeline before flashing.

Why a new format instead of reusing .tyolo:
  * The target is built with -mgeneral-regs-only (NO FPU).  .tyolo stores bias
    and anchors as float32; those must be pre-converted to integers on the host.
  * Conv is executed on-device as im2col + tiled INT8 matmul (mirroring
    sim_fixedpoint.py::_im2col_matmul), reusing the proven NPU matmul path, so
    the bundle carries plain [oc][ic][kh][kw] int8 weights + int64 bias_q.

The `simulate()` path re-implements EXACTLY the integer arithmetic the C code
will run (including fixed-point sigmoid/box-decode/NMS), so a match here means
the device will reproduce the golden INT8 detections.

Usage (run with a Python that has numpy; torch/cv2 only needed via ref_forward):
  D:/Py/python.exe export_device.py --npz weights/yolov5n_int8.npz \
        --image weights/bus.jpg --output weights/yolov5n.ydev --sim
"""

import argparse, struct, sys
import numpy as np
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import quantize as Q
from ref_forward import preprocess_image, COCO_NAMES

YDEV_MAGIC = 0x56454459  # "YDEV"
SHIFT = 20

# op types (identical to quantize.OP_*)
OP_CONV, OP_CONV_LIN, OP_MAXPOOL, OP_UPSAMPLE, OP_CONCAT, OP_ADD = 0, 1, 2, 3, 4, 5

OP_ENTRY = 32  # bytes, 8 x uint32


# ─────────────────────────────────────────────────────────────
#  sigmoid LUT (parsed from the on-device npu_math.c so host & device agree)
# ─────────────────────────────────────────────────────────────
def load_sigmoid_lut(math_c):
    import re
    txt = open(math_c).read()
    m = re.search(r'sigmoid_lut\[256\]\s*=\s*\{(.+?)\};', txt, re.S)
    vals = [int(x) for x in re.findall(r'-?\d+', m.group(1))][:256]
    return np.array(vals, dtype=np.int64)


# ─────────────────────────────────────────────────────────────
#  Load quantized weights + rebuild the flat op graph from the npz
# ─────────────────────────────────────────────────────────────
def load_bundle(npz_path):
    d = np.load(npz_path, allow_pickle=True)
    anchors = d['anchors']          # [3,3,2] float grid units
    strides = d['strides']          # [3] float
    nc = int(d['nc'][0])

    # reconstruct fp-less weight dict just to feed build_op_graph (needs shapes)
    layers = {}
    W = {}
    for k in d.files:
        if not k.endswith('_w_int8'):
            continue
        safe = k[:-7]
        name = safe.replace('_', '.')
        w_i8 = d[f'{safe}_w_int8']                    # int8 [oc,ic,kh,kw]
        ws_f = float(d[f'{safe}_w_scale'][0])
        b_f = d[f'{safe}_bias'].astype(np.float64)
        ws_q20 = max(1, int(round(ws_f * (1 << SHIFT))))
        if ws_f > 1e-12:
            b_q = np.round(b_f / ws_f * (1 << SHIFT)).astype(np.int64)
        else:
            b_q = np.zeros(len(b_f), dtype=np.int64)
        layers[name] = {'weight': w_i8}
        W[name] = {'w': w_i8.astype(np.int64), 'ws': ws_q20, 'bq': b_q,
                   'w_i8': w_i8.astype(np.int8)}

    ops, detect_idx = Q.build_op_graph(layers)
    return W, ops, detect_idx, anchors, strides, nc


# ─────────────────────────────────────────────────────────────
#  Write the .ydev binary
# ─────────────────────────────────────────────────────────────
def build_ydev(W, ops, detect_idx, anchors, strides, nc,
               x_i8, x_s_q20, orig_hw, pad_hw, inv_scale_q16, out_path):
    n_ops = len(ops)
    n_det = len(detect_idx)

    # --- assemble weight blob: per conv-op, int8 weights then int64 bias ---
    blob = bytearray()
    conv_off = {}   # op output index -> (w_off_rel, b_off_rel, w_scale_q20)
    for op in ops:
        if op['type'] not in (OP_CONV, OP_CONV_LIN):
            continue
        name = op['conv_name']
        w = W[name]
        w_rel = len(blob)
        blob.extend(w['w_i8'].tobytes())            # [oc][ic][kh][kw] int8
        while len(blob) % 8:                        # align bias to 8
            blob.append(0)
        b_rel = len(blob)
        blob.extend(w['bq'].astype('<i8').tobytes())  # int64 bias_q
        conv_off[op['output']] = (w_rel, b_rel, w['ws'])

    # --- input int8 (CHW) ---
    in_rel = len(blob)
    blob.extend(x_i8.astype(np.int8).tobytes())     # [3][320][320]

    # --- fixed-layout sections ---
    header_size = 64
    det_size = ((n_det * 2 + 3) & ~3)               # u16, pad to 4
    anchors_size = 3 * 3 * 2 * 4                     # i32 Q16
    strides_size = 3 * 4                             # i32
    optab_size = n_ops * OP_ENTRY
    data_base = header_size + det_size + anchors_size + strides_size + optab_size
    data_base = (data_base + 15) & ~15

    def abs_off(rel):
        return data_base + rel

    buf = bytearray()
    # header (16 x u32)
    buf.extend(struct.pack('<16I',
        YDEV_MAGIC, 1, nc, n_ops, n_det, 320, 3, x_s_q20,
        orig_hw[0], orig_hw[1], pad_hw[0], pad_hw[1],
        inv_scale_q16, abs_off(in_rel), 0, 0))
    # detect indices
    for di in detect_idx:
        buf.extend(struct.pack('<H', di))
    while len(buf) < header_size + det_size:
        buf.append(0)
    # anchors Q16
    for s in range(3):
        for a in range(3):
            for t in range(2):
                buf.extend(struct.pack('<i', int(round(float(anchors[s, a, t]) * 65536))))
    # strides (int)
    for s in range(3):
        buf.extend(struct.pack('<i', int(round(float(strides[s])))))
    # op table
    for op in ops:
        t = op['type']
        st = op.get('stride', 0)
        pad = op.get('padding', 0)
        ia = op['input'] if isinstance(op['input'], int) else op['input'][0]
        ib = 0xFFFF if isinstance(op['input'], int) else op['input'][1]
        out = op['output']
        if t in (OP_CONV, OP_CONV_LIN):
            w = W[op['conv_name']]['w_i8']
            oc, ic, kh, kw = w.shape
            w_off, b_off, ws = conv_off[out]
            w0 = (t & 0xFF) | ((st & 0xFF) << 8) | ((pad & 0xFF) << 16)
            w1 = (ia & 0xFFFF) | ((ib & 0xFFFF) << 16)
            w2 = (out & 0xFFFF) | ((oc & 0xFFFF) << 16)
            w3 = (ic & 0xFFFF) | ((kh & 0xFF) << 16) | ((kw & 0xFF) << 24)
            buf.extend(struct.pack('<8I', w0, w1, w2, w3,
                                   abs_off(w_off), abs_off(b_off), ws, 0))
        else:
            w0 = (t & 0xFF)
            w1 = (ia & 0xFFFF) | ((ib & 0xFFFF) << 16)
            w2 = (out & 0xFFFF)
            buf.extend(struct.pack('<8I', w0, w1, w2, 0, 0, 0, 0, 0))
    while len(buf) < data_base:
        buf.append(0)
    buf.extend(blob)

    Path(out_path).write_bytes(buf)
    print(f"Wrote {out_path}: {len(buf):,} bytes "
          f"(header+tables={data_base}, blob={len(blob):,})")
    print(f"  n_ops={n_ops} n_conv={len(conv_off)} in_off={abs_off(in_rel)} "
          f"x_s_q20={x_s_q20} inv_scale_q16={inv_scale_q16}")
    return bytes(buf)


# ═════════════════════════════════════════════════════════════
#  C-MODEL: exact integer forward + fixed-point postprocessing.
#  Every operation here is integer and mirrors the C I will write.
# ═════════════════════════════════════════════════════════════
class CModel:
    def __init__(self, W, ops, detect_idx, anchors, strides, nc, sig_lut):
        self.W, self.ops, self.detect_idx = W, ops, detect_idx
        self.anchors, self.strides, self.nc = anchors, strides, nc
        self.sig = sig_lut

    @staticmethod
    def _floordiv(a, b):
        # numpy int64 // already floors; kept explicit to mirror C floor_div
        return a // b

    @staticmethod
    def _requant(acc):
        mx = int(np.max(np.abs(acc)))
        if mx < 1:
            mx = 1
        oi8 = np.clip((acc * 127 * 2 + np.sign(acc) * mx) // (mx * 2),
                      -128, 127).astype(np.int64)
        return oi8, mx

    def _conv_acc(self, x_i8, name, stride, padding, x_s):
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
        acc = acc.reshape(oc, oh, ow)
        b = self._floordiv(self.W[name]['bq'], x_s)
        return acc + b.reshape(-1, 1, 1), oc

    def conv_silu(self, x_i8, x_s, name, stride, padding):
        ws = self.W[name]['ws']
        acc, _ = self._conv_acc(x_i8, name, stride, padding, x_s)
        y_i8, y_mx = self._requant(acc)
        y_s = max(1, (y_mx * ws * x_s) // (127 * (1 << SHIFT)))
        idx = np.clip((y_i8 * y_s * 32) >> SHIFT, -128, 127)
        sig = self.sig[(idx & 0xFF).astype(np.intp)] + 128
        out_i8, silu_mx = self._requant(y_i8 * sig)
        out_s = max(1, (silu_mx * y_s) // (127 * 255))
        return out_i8, out_s

    def conv_linear(self, x_i8, x_s, name):
        ws = self.W[name]['ws']
        acc, _ = self._conv_acc(x_i8, name, 1, 0, x_s)
        out_i8, mx = self._requant(acc)
        out_s = max(1, (mx * ws * x_s) // (127 * (1 << SHIFT)))
        return out_i8, out_s

    def maxpool(self, x_i8, x_s):
        C, H, Wd = x_i8.shape
        p = 2
        xp = np.full((C, H + 2*p, Wd + 2*p), -128, dtype=np.int64)
        xp[:, p:p+H, p:p+Wd] = x_i8
        oh, ow = H, Wd
        out = np.full((C, oh, ow), -128, dtype=np.int64)
        for i in range(oh):
            for j in range(ow):
                out[:, i, j] = xp[:, i:i+5, j:j+5].reshape(C, -1).max(axis=1)
        return out, x_s

    def upsample(self, x_i8, x_s):
        return np.repeat(np.repeat(x_i8, 2, axis=1), 2, axis=2), x_s

    def concat(self, a, a_s, b, b_s):
        if a_s >= b_s:
            b_r = np.clip((b * b_s + a_s // 2) // a_s, -128, 127).astype(np.int64)
            return np.concatenate([a, b_r], axis=0), a_s
        a_r = np.clip((a * a_s + b_s // 2) // b_s, -128, 127).astype(np.int64)
        return np.concatenate([a_r, b], axis=0), b_s

    def add_res(self, a, a_s, b, b_s):
        oi8, mx = self._requant(a * a_s + b * b_s)
        return oi8, max(1, mx // 127)

    def forward(self, x_i8, x_s):
        res = [None] * len(self.ops)
        scl = [0] * len(self.ops)
        for op in self.ops:
            t, o = op['type'], op['output']
            ia = op['input'] if isinstance(op['input'], int) else op['input'][0]
            xin = x_i8 if ia == -1 else res[ia]
            xs = x_s if ia == -1 else scl[ia]
            if t == OP_CONV:
                res[o], scl[o] = self.conv_silu(xin, xs, op['conv_name'],
                                                op['stride'], op['padding'])
            elif t == OP_CONV_LIN:
                res[o], scl[o] = self.conv_linear(xin, xs, op['conv_name'])
            elif t == OP_MAXPOOL:
                res[o], scl[o] = self.maxpool(xin, xs)
            elif t == OP_UPSAMPLE:
                res[o], scl[o] = self.upsample(xin, xs)
            elif t == OP_CONCAT:
                ib = op['input'][1]
                res[o], scl[o] = self.concat(xin, xs, res[ib], scl[ib])
            elif t == OP_ADD:
                ib = op['input'][1]
                res[o], scl[o] = self.add_res(xin, xs, res[ib], scl[ib])
        dets = [(res[i], scl[i]) for i in self.detect_idx]
        return dets

    # ── fixed-point postprocessing ──
    def _sigmoid_q16(self, val_q20):
        """sigmoid(real) as Q16, real carried as int64 Q20. Reuses sigmoid_lut
        exactly as the SiLU path: idx≈real*32, lut≈round(sigmoid*255)-128."""
        idx = (val_q20 * 32) >> SHIFT
        if idx > 127:
            idx = 127
        if idx < -128:
            idx = -128
        s255 = int(self.sig[idx & 0xFF]) + 128            # 0..255 ~ sigmoid*255
        return (s255 * 65536) // 255                       # Q16

    def decode(self, dets, conf_q16):
        na, no = 3, 5 + self.nc
        boxes = []
        for si, (di8, ds) in enumerate(dets):
            _, gh, gw = di8.shape
            di8 = di8.astype(np.int64)
            stride = int(round(float(self.strides[si])))
            for a in range(na):
                aw_q16 = int(round(float(self.anchors[si, a, 0]) * 65536))
                ah_q16 = int(round(float(self.anchors[si, a, 1]) * 65536))
                base = a * no
                for gy in range(gh):
                    for gx in range(gw):
                        obj_q20 = int(di8[base + 4, gy, gx]) * ds
                        obj = self._sigmoid_q16(obj_q20)
                        if obj < conf_q16:
                            continue
                        # class argmax over sigmoid (monotonic → argmax on logit)
                        cls_id, best_logit = 0, None
                        for c in range(self.nc):
                            lg = int(di8[base + 5 + c, gy, gx])
                            if best_logit is None or lg > best_logit:
                                best_logit, cls_id = lg, c
                        cls_q16 = self._sigmoid_q16(best_logit * ds)
                        score = (obj * cls_q16) >> 16
                        if score < conf_q16:
                            continue
                        tx = self._sigmoid_q16(int(di8[base+0, gy, gx]) * ds)
                        ty = self._sigmoid_q16(int(di8[base+1, gy, gx]) * ds)
                        tw = self._sigmoid_q16(int(di8[base+2, gy, gx]) * ds)
                        th = self._sigmoid_q16(int(di8[base+3, gy, gx]) * ds)
                        # cx = (tx*2 - 0.5 + gx) * stride  (Q16 px in 320 space)
                        cx = ((tx*2 - 32768 + gx*65536) * stride)
                        cy = ((ty*2 - 32768 + gy*65536) * stride)
                        # w = (tw*2)^2 * anchor_w * stride
                        tw2 = tw * 2
                        wsq = (tw2 * tw2) >> 16
                        th2 = th * 2
                        hsq = (th2 * th2) >> 16
                        w = (((wsq * aw_q16) >> 16) * stride)
                        h = (((hsq * ah_q16) >> 16) * stride)
                        x1 = cx - w // 2
                        y1 = cy - h // 2
                        x2 = cx + w // 2
                        y2 = cy + h // 2
                        boxes.append([x1, y1, x2, y2, score, cls_id])
        return boxes

    @staticmethod
    def nms(boxes, iou_q16):
        # coords carried as Q16; drop to integer pixels for IoU so int64 is safe.
        # areas ~1e6, inter<<16 ~1e11 — well within int64.
        b_px = [[b[0] >> 16, b[1] >> 16, b[2] >> 16, b[3] >> 16, b[4], b[5], i]
                for i, b in enumerate(boxes)]
        keep = []
        classes = set(b[5] for b in b_px)
        for cls in classes:
            cb = sorted([b for b in b_px if b[5] == cls], key=lambda b: -b[4])
            while cb:
                best = cb.pop(0)
                keep.append(boxes[best[6]])
                bx1, by1, bx2, by2 = best[:4]
                barea = (bx2 - bx1) * (by2 - by1)
                rest = []
                for c in cb:
                    ix1 = max(bx1, c[0]); iy1 = max(by1, c[1])
                    ix2 = min(bx2, c[2]); iy2 = min(by2, c[3])
                    iw = ix2 - ix1; ih = iy2 - iy1
                    if iw <= 0 or ih <= 0:
                        rest.append(c); continue
                    inter = iw * ih
                    carea = (c[2]-c[0]) * (c[3]-c[1])
                    uni = barea + carea - inter
                    if uni <= 0:
                        rest.append(c); continue
                    iou = (inter << 16) // uni             # Q16
                    if iou <= iou_q16:
                        rest.append(c)
                cb = rest
        return keep

    def rescale(self, boxes, orig_hw, pad_hw, inv_scale_q16):
        oh, ow = orig_hw
        ph, pw = pad_hw
        out = []
        for b in boxes:
            xs = []
            for k, (coord, pad) in enumerate([(b[0], pw), (b[1], ph),
                                              (b[2], pw), (b[3], ph)]):
                # px_letterbox = coord/65536 ; orig = (px - pad) * inv_scale
                v = ((coord - pad * 65536) * inv_scale_q16) >> 32
                lim = ow if k % 2 == 0 else oh
                v = max(0, min(v, lim))
                xs.append(int(v))
            out.append([xs[0], xs[1], xs[2], xs[3], b[4], b[5]])
        return out


def run_sim(W, ops, detect_idx, anchors, strides, nc, sig_lut,
            x_i8, x_s, orig_hw, pad_hw, inv_scale_q16):
    m = CModel(W, ops, detect_idx, anchors, strides, nc, sig_lut)
    dets = m.forward(x_i8.astype(np.int64), x_s)
    conf_q16 = int(0.25 * 65536)
    iou_q16 = int(0.45 * 65536)
    boxes = m.decode(dets, conf_q16)
    print(f"  Pre-NMS: {len(boxes)}")
    boxes = m.nms(boxes, iou_q16)
    print(f"  Post-NMS: {len(boxes)}")
    boxes = m.rescale(boxes, orig_hw, pad_hw, inv_scale_q16)
    boxes.sort(key=lambda b: -b[4])
    print("\nC-model INT detections:")
    for i, b in enumerate(boxes):
        name = COCO_NAMES[b[5]] if b[5] < len(COCO_NAMES) else f"cls_{b[5]}"
        print(f"  [{i}] {name}: conf={b[4]/65536:.3f} "
              f"[{b[0]},{b[1]},{b[2]},{b[3]}]")
    return boxes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--npz', default='weights/yolov5n_int8.npz')
    ap.add_argument('--image', default='weights/bus.jpg')
    ap.add_argument('--output', default='weights/yolov5n.ydev')
    ap.add_argument('--math_c', default=str(Path(__file__).resolve().parent.parent.parent
                                            / 'src/npulib/npu_math.c'))
    ap.add_argument('--sim', action='store_true', help='run the C-model verifier')
    args = ap.parse_args()

    sig_lut = load_sigmoid_lut(args.math_c)
    W, ops, detect_idx, anchors, strides, nc = load_bundle(args.npz)
    print(f"Loaded {len(W)} convs, {len(ops)} ops, nc={nc}")

    # preprocess + quantize input exactly as sim_fixedpoint.forward()
    x_f, orig_hw, (scale, pad_h, pad_w) = preprocess_image(args.image, 320)
    mx = float(np.max(np.abs(x_f)))
    if mx < 1e-10:
        mx = 1e-10
    in_s_f = mx / 127.0
    x_i8 = np.clip(np.round(x_f / in_s_f), -128, 127).astype(np.int64)
    x_s = max(1, int(round(in_s_f * (1 << SHIFT))))
    inv_scale_q16 = int(round((1.0 / scale) * 65536))

    build_ydev(W, ops, detect_idx, anchors, strides, nc,
               x_i8, x_s, orig_hw, (pad_h, pad_w), inv_scale_q16, args.output)

    if args.sim:
        print("\n=== C-model integer simulation ===")
        run_sim(W, ops, detect_idx, anchors, strides, nc, sig_lut,
                x_i8, x_s, orig_hw, (pad_h, pad_w), inv_scale_q16)


if __name__ == '__main__':
    main()

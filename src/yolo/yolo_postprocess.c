#include "yolo/yolo.h"
#include "npulib/npu_math.h"
#include "lib/t_logger.h"
#include "dev/t_timer.h"

/*
 * Fixed-point post-processing — exact port of export_device.py::CModel
 * {decode, nms, rescale}.  No floating point.
 *
 * Detect tensors are int8 [na*(5+nc)][gh][gw] (CHW) with a Q20 scale.
 * Sigmoid reuses sigmoid_lut (idx≈real*32, lut≈round(sigmoid*255)-128), giving
 * a Q16 probability.  Box coords are carried in Q16 pixels (320-space) until
 * rescale maps them back to the original image.
 */

#define SHIFT YOLO_SCALE_SHIFT
#define S8(p, i) ((int32_t)(signed char)(p)[(i)])

#define MAX_BOXES 512

typedef struct {
    int32_t x1, y1, x2, y2;   /* Q16 pixels (320-space) */
    uint32_t score;           /* Q16 */
    uint16_t cls;
} det_box_t;

static const char *const COCO_NAMES[80] = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck",
    "boat","traffic light","fire hydrant","stop sign","parking meter","bench",
    "bird","cat","dog","horse","sheep","cow","elephant","bear","zebra",
    "giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
    "skis","snowboard","sports ball","kite","baseball bat","baseball glove",
    "skateboard","surfboard","tennis racket","bottle","wine glass","cup",
    "fork","knife","spoon","bowl","banana","apple","sandwich","orange",
    "broccoli","carrot","hot dog","pizza","donut","cake","chair","couch",
    "potted plant","bed","dining table","toilet","tv","laptop","mouse",
    "remote","keyboard","cell phone","microwave","oven","toaster","sink",
    "refrigerator","book","clock","vase","scissors","teddy bear",
    "hair drier","toothbrush"
};

/* sigmoid(real) as Q16, real carried as int64 Q20. Same LUT path as SiLU. */
static int32_t
sigmoid_q16(int64_t val_q20)
{
    int64_t idx = (val_q20 * 32) >> SHIFT;
    if (idx > 127) idx = 127;
    if (idx < -128) idx = -128;
    int32_t s255 = (int32_t)(signed char)sigmoid_lut[idx & 0xFF] + 128; /* 0..255 */
    return (int32_t)(((int64_t)s255 * 65536) / 255);
}

static int
decode(yolo_model_t *m, uint32_t conf_q16, det_box_t *boxes, int max_boxes)
{
    int nc = (int)m->hdr.nc;
    int na = 3, no = 5 + nc;
    int nboxes = 0;

    for (int si = 0; si < (int)m->hdr.n_detect; si++) {
        const yolo_tensor_t *d = &m->results[m->detect_idx[si]];
        int gh = d->h, gw = d->w;
        int plane = gh * gw;
        int64_t ds = d->scale_q20;
        int stride = m->strides[si];

        for (int a = 0; a < na; a++) {
            int64_t aw_q16 = m->anchors_q16[si][a][0];
            int64_t ah_q16 = m->anchors_q16[si][a][1];
            int base = a * no;
            const int8_t *obj_ch = d->data + (int64_t)(base + 4) * plane;

            for (int gy = 0; gy < gh; gy++) {
                for (int gx = 0; gx < gw; gx++) {
                    int cell = gy * gw + gx;
                    int32_t obj = sigmoid_q16((int64_t)S8(obj_ch, cell) * ds);
                    if ((uint32_t)obj < conf_q16)
                        continue;

                    /* class argmax on the raw logit (sigmoid is monotonic) */
                    int cls_id = 0;
                    int32_t best_logit = S8(d->data + (int64_t)(base + 5) * plane, cell);
                    for (int c = 1; c < nc; c++) {
                        int32_t lg = S8(d->data + (int64_t)(base + 5 + c) * plane, cell);
                        if (lg > best_logit) { best_logit = lg; cls_id = c; }
                    }
                    int32_t cls_q16 = sigmoid_q16((int64_t)best_logit * ds);
                    uint32_t score = (uint32_t)(((int64_t)obj * cls_q16) >> 16);
                    if (score < conf_q16)
                        continue;

                    int64_t tx = sigmoid_q16((int64_t)S8(d->data + (int64_t)(base+0)*plane, cell) * ds);
                    int64_t ty = sigmoid_q16((int64_t)S8(d->data + (int64_t)(base+1)*plane, cell) * ds);
                    int64_t tw = sigmoid_q16((int64_t)S8(d->data + (int64_t)(base+2)*plane, cell) * ds);
                    int64_t th = sigmoid_q16((int64_t)S8(d->data + (int64_t)(base+3)*plane, cell) * ds);

                    /* cx = (tx*2 - 0.5 + gx) * stride  (Q16 * px) */
                    int64_t cx = (tx * 2 - 32768 + (int64_t)gx * 65536) * stride;
                    int64_t cy = (ty * 2 - 32768 + (int64_t)gy * 65536) * stride;
                    int64_t tw2 = tw * 2, th2 = th * 2;
                    int64_t wsq = (tw2 * tw2) >> 16;
                    int64_t hsq = (th2 * th2) >> 16;
                    int64_t w = ((wsq * aw_q16) >> 16) * stride;
                    int64_t h = ((hsq * ah_q16) >> 16) * stride;

                    if (nboxes >= max_boxes)
                        continue;
                    det_box_t *bx = &boxes[nboxes++];
                    bx->x1 = (int32_t)(cx - w / 2);
                    bx->y1 = (int32_t)(cy - h / 2);
                    bx->x2 = (int32_t)(cx + w / 2);
                    bx->y2 = (int32_t)(cy + h / 2);
                    bx->score = score;
                    bx->cls = (uint16_t)cls_id;
                }
            }
        }
    }
    return nboxes;
}

/* per-class greedy NMS on integer-pixel boxes. keep[] holds surviving indices. */
static int
nms(det_box_t *boxes, int n, uint32_t iou_q16, int *keep)
{
    /* integer-pixel copies for IoU (coords are Q16 → >>16) */
    static int px[MAX_BOXES][4];
    static uint8_t removed[MAX_BOXES];
    for (int i = 0; i < n; i++) {
        px[i][0] = boxes[i].x1 >> 16;
        px[i][1] = boxes[i].y1 >> 16;
        px[i][2] = boxes[i].x2 >> 16;
        px[i][3] = boxes[i].y2 >> 16;
        removed[i] = 0;
    }
    int nkeep = 0;
    for (;;) {
        /* pick highest-scoring not-yet-removed box */
        int best = -1;
        uint32_t best_score = 0;
        for (int i = 0; i < n; i++) {
            if (removed[i]) continue;
            if (best < 0 || boxes[i].score > best_score) {
                best = i; best_score = boxes[i].score;
            }
        }
        if (best < 0)
            break;
        removed[best] = 1;
        keep[nkeep++] = best;

        int bx1 = px[best][0], by1 = px[best][1], bx2 = px[best][2], by2 = px[best][3];
        int64_t barea = (int64_t)(bx2 - bx1) * (by2 - by1);
        for (int i = 0; i < n; i++) {
            if (removed[i] || boxes[i].cls != boxes[best].cls)
                continue;
            int ix1 = bx1 > px[i][0] ? bx1 : px[i][0];
            int iy1 = by1 > px[i][1] ? by1 : px[i][1];
            int ix2 = bx2 < px[i][2] ? bx2 : px[i][2];
            int iy2 = by2 < px[i][3] ? by2 : px[i][3];
            int iw = ix2 - ix1, ih = iy2 - iy1;
            if (iw <= 0 || ih <= 0)
                continue;
            int64_t inter = (int64_t)iw * ih;
            int64_t carea = (int64_t)(px[i][2]-px[i][0]) * (px[i][3]-px[i][1]);
            int64_t uni = barea + carea - inter;
            if (uni <= 0)
                continue;
            int64_t iou = (inter << 16) / uni;   /* Q16 */
            if (iou > (int64_t)iou_q16)
                removed[i] = 1;
        }
    }
    return nkeep;
}

void
yolo_postprocess(yolo_model_t *m, uint32_t conf_q16, uint32_t iou_q16)
{
    static det_box_t boxes[MAX_BOXES];
    static int keep[MAX_BOXES];

    uint64_t t_dec0 = timer_get_system_ticks();
    int n = decode(m, conf_q16, boxes, MAX_BOXES);
    uint64_t t_dec1 = timer_get_system_ticks();
    int nk = nms(boxes, n, iou_q16, keep);
    uint64_t t_nms1 = timer_get_system_ticks();

    uint64_t dec_us = (t_dec1 - t_dec0) * 1000000 / TIMER_FREQUENCY_HZ;
    uint64_t nms_us = (t_nms1 - t_dec1) * 1000000 / TIMER_FREQUENCY_HZ;
    logger_info("YOLO: decode %d boxes in %llu us, NMS -> %d in %llu us\n",
                n, dec_us, nk, nms_us);

    /* rescale from letterboxed 320-space (Q16) to original image px */
    int oh = (int)m->hdr.orig_h, ow = (int)m->hdr.orig_w;
    int ph = (int)m->hdr.pad_h, pw = (int)m->hdr.pad_w;
    int64_t inv = (int64_t)m->hdr.inv_scale_q16;

    logger_info("YOLO detections:\n");
    for (int i = 0; i < nk; i++) {
        det_box_t *b = &boxes[keep[i]];
        int32_t coords[4]; int pads[4] = {pw, ph, pw, ph}; int lims[4] = {ow, oh, ow, oh};
        int32_t raw[4] = {b->x1, b->y1, b->x2, b->y2};
        for (int k = 0; k < 4; k++) {
            /* orig = (px_q16 - pad*65536) * inv_scale_q16 / 2^32 */
            int64_t v = (((int64_t)raw[k] - (int64_t)pads[k] * 65536) * inv) >> 32;
            if (v < 0) v = 0;
            if (v > lims[k]) v = lims[k];
            coords[k] = (int32_t)v;
        }
        const char *name = b->cls < 80 ? COCO_NAMES[b->cls] : "?";
        /* score is Q16; print as thousandths */
        uint32_t milli = (uint32_t)(((uint64_t)b->score * 1000) >> 16);
        logger_info("  [%d] %s: conf=0.%03u [%d,%d,%d,%d]\n",
                    i, name, milli, coords[0], coords[1], coords[2], coords[3]);
    }
}

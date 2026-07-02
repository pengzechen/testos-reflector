#include "yolo/yolo.h"
#include "npulib/npu_matmul.h"
#include "mem/cache.h"
#include "mem/t_mem.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"

/*
 * yolo_model_load — parse a .ydev image (produced by tools/yolo/export_device.py)
 * into a runtime model, allocate DMA scratch + per-op result buffers, and
 * pre-lay-out every conv weight into a resident NC1HWC2 buffer (mirrors
 * llm_prelayout_weight, so the per-op matmul skips weight layout).
 */

/* worst-case sizes for YOLOv5n 320x320 (computed offline in tools/yolo):
 *   maxM=25600 maxK=1152 maxN=256, im2col=2.76MB, feat=2.87MB, out=1.6MB,
 *   max tensor=409600 int8, max weight prelayout=294912. */
#define YOLO_MAX_M       25600
#define YOLO_MAX_K       1152
#define YOLO_FEAT_BYTES  (2867200 + 4096)
#define YOLO_OUT_BYTES   (1638400 + 4096)
#define YOLO_IM2COL_SZ   (2764800 + 4096)

static int8_t *
prelayout_conv_weight(const yolo_op_t *op)
{
    /* weight_int8 tiling with C = in_c*kh*kw (im2col K), N = out_c.
     * matches llm_prelayout_weight but for conv shapes.
     *
     * K and N are padded up to multiples of 32: the RK3588 INT8 datapath tiles
     * channels in groups of 32, and non-aligned dims corrupt the descriptor
     * geometry (NPU hangs). Padding is arithmetically free — padded weights are
     * zero, padded output kernels are never read back. */
    int N = op->out_c;
    int K = (int)op->in_c * op->kh * op->kw;
    uint32_t K_pad = ((K + 31) / 32) * 32;
    uint32_t N_pad = ((N + 31) / 32) * 32;
    uint32_t wt_size = K_pad * N_pad;

    int8_t *wbuf = (int8_t *)t_mem_alloc(wt_size);
    if (!wbuf)
        return NULL;
    memset(wbuf, 0, wt_size);

    /* op->weights is [oc][ic][kh][kw]; im2col flattens the patch as
     * [ic][kh][kw] in the SAME order (see yolo_conv im2col), so the flat
     * channel index k = ic*kh*kw + kr*kw + kc maps 1:1. Lay out with the
     * PADDED K_pad as the tiling stride so it matches the padded matmul. */
    const int8_t *src = op->weights;
    for (int n = 0; n < N; n++) {
        for (int k = 0; k < K; k++) {
            int dst = weight_int8((int)K_pad, n + 1, k + 1);
            wbuf[dst] = src[n * K + k];
        }
    }
    clean_dcache_va_range(wbuf, wt_size);
    return wbuf;
}

int
yolo_model_load(yolo_model_t *m, const void *ydev, uint32_t size)
{
    const uint8_t *base = (const uint8_t *)ydev;
    memset(m, 0, sizeof(*m));
    m->base = base;

    const ydev_header_t *h = (const ydev_header_t *)base;
    if (h->magic != YDEV_MAGIC) {
        logger_error("YOLO: bad magic 0x%x (expected YDEV)\n", h->magic);
        return -1;
    }
    m->hdr = *h;
    logger_info("YOLO: nc=%d n_ops=%d n_det=%d img=%d in_scale_q20=%d\n",
                h->nc, h->n_ops, h->n_detect, h->img_size, h->in_scale_q20);
    logger_info("YOLO: orig=%dx%d pad=(%d,%d) inv_scale_q16=%d\n",
                h->orig_h, h->orig_w, h->pad_h, h->pad_w, h->inv_scale_q16);

    if (h->n_ops > YOLO_MAX_OPS) {
        logger_error("YOLO: n_ops %d > %d\n", h->n_ops, YOLO_MAX_OPS);
        return -2;
    }

    uint32_t pos = sizeof(ydev_header_t);

    /* detect indices (u16), padded to 4 bytes */
    for (uint32_t i = 0; i < h->n_detect; i++)
        m->detect_idx[i] = *(const uint16_t *)(base + pos + i * 2);
    pos += (h->n_detect * 2 + 3) & ~3u;

    /* anchors Q16 [3][3][2] i32 */
    for (int s = 0; s < 3; s++)
        for (int a = 0; a < 3; a++)
            for (int t = 0; t < 2; t++) {
                m->anchors_q16[s][a][t] = *(const int32_t *)(base + pos);
                pos += 4;
            }
    /* strides i32 */
    for (int s = 0; s < 3; s++) {
        m->strides[s] = *(const int32_t *)(base + pos);
        pos += 4;
    }

    /* op table */
    const ydev_op_t *raw = (const ydev_op_t *)(base + pos);
    m->n_ops = h->n_ops;
    for (uint32_t i = 0; i < h->n_ops; i++) {
        const ydev_op_t *r = &raw[i];
        yolo_op_t *op = &m->ops[i];
        op->type   = r->w0 & 0xFF;
        op->stride = (r->w0 >> 8) & 0xFF;
        op->pad    = (r->w0 >> 16) & 0xFF;
        op->in_a   = (int16_t)(r->w1 & 0xFFFF);
        op->in_b   = (int16_t)((r->w1 >> 16) & 0xFFFF);
        op->out    = r->w2 & 0xFFFF;
        op->out_c  = (r->w2 >> 16) & 0xFFFF;
        op->in_c   = r->w3 & 0xFFFF;
        op->kh     = (r->w3 >> 16) & 0xFF;
        op->kw     = (r->w3 >> 24) & 0xFF;
        op->w_scale_q20 = r->w_scale_q20;
        op->weights = NULL;
        op->bias_q  = NULL;
        op->dma_w   = NULL;
        if (op->type == YOP_CONV || op->type == YOP_CONV_LIN) {
            op->weights = (const int8_t *)(base + r->w_off);
            op->bias_q  = (const int64_t *)(base + r->b_off);
        }
    }

    /* input tensor */
    m->input.data = (int8_t *)(base + h->in_offset);
    m->input.c = h->in_channels;
    m->input.h = h->img_size;
    m->input.w = h->img_size;
    m->input.scale_q20 = h->in_scale_q20;

    /* DMA scratch buffers, sized for the largest layer */
    m->dma_input  = (int8_t *)t_mem_alloc(YOLO_FEAT_BYTES);
    m->dma_output = (int8_t *)t_mem_alloc(YOLO_OUT_BYTES);
    m->dma_regs   = (uint64_t *)t_mem_alloc(108 * YOLO_MAX_TILES * sizeof(uint64_t));
    m->dma_regcmd = t_mem_alloc(108 * YOLO_MAX_TILES * sizeof(uint64_t));
    m->dma_tasks  = t_mem_alloc(YOLO_MAX_TILES * 1024);
    m->im2col     = (int8_t *)t_mem_alloc(YOLO_IM2COL_SZ);
    if (!m->dma_input || !m->dma_output || !m->dma_regs ||
        !m->dma_regcmd || !m->dma_tasks || !m->im2col) {
        logger_error("YOLO: DMA/scratch alloc failed\n");
        return -3;
    }

    /* pre-lay-out conv weights into resident buffers */
    uint32_t n_conv = 0;
    for (uint32_t i = 0; i < m->n_ops; i++) {
        yolo_op_t *op = &m->ops[i];
        if (op->type == YOP_CONV || op->type == YOP_CONV_LIN) {
            op->dma_w = prelayout_conv_weight(op);
            if (!op->dma_w) {
                logger_error("YOLO: prelayout OOM at op %d\n", i);
                return -4;
            }
            n_conv++;
        }
    }

    (void)size;
    logger_info("YOLO: loaded, %d conv weights pre-laid-out\n", n_conv);
    return 0;
}

#ifndef YOLO_H
#define YOLO_H

#include "t_types.h"

/*
 * YOLOv5n INT8 bare-metal inference for RK3588 NPU.
 *
 * Integer-only port of tools/yolo/sim_fixedpoint.py, driven by an on-device
 * op-table interpreter.  Conv runs as im2col + tiled INT8 NPU matmul (the same
 * proven path llm_npu_matmul uses); everything else (bias-add, requant, SiLU,
 * maxpool, upsample, concat, residual add) and all post-processing is exact
 * integer / fixed-point arithmetic — the target has no FPU.
 *
 * File format (.ydev) is produced by tools/yolo/export_device.py.  Every scale
 * is Q20 (real = int8 * scale / 2^20); box coords in post-processing are Q16.
 *
 * NOTE: int8_t / char is UNSIGNED on this ARM toolchain.  Every read of an int8
 * activation/weight value MUST go through (signed char).
 */

#define YDEV_MAGIC       0x56454459u   /* "YDEV" */
#define YOLO_SCALE_SHIFT 20

/* op types (mirror tools/yolo/quantize.py OP_*) */
#define YOP_CONV      0   /* conv + bias + SiLU        */
#define YOP_CONV_LIN  1   /* conv + bias (detect head) */
#define YOP_MAXPOOL   2   /* maxpool 5x5 s1 p2         */
#define YOP_UPSAMPLE  3   /* nearest 2x                */
#define YOP_CONCAT    4   /* channel concat (2 inputs) */
#define YOP_ADD       5   /* residual add (2 inputs)   */

/* ── .ydev header: 16 x uint32 (64 bytes) ── */
typedef struct {
    uint32_t magic;        /* YDEV_MAGIC */
    uint32_t version;
    uint32_t nc;           /* num classes (80) */
    uint32_t n_ops;        /* 87 */
    uint32_t n_detect;     /* 3 */
    uint32_t img_size;     /* 320 */
    uint32_t in_channels;  /* 3 */
    uint32_t in_scale_q20; /* input activation scale */
    uint32_t orig_h;       /* original image height */
    uint32_t orig_w;       /* original image width */
    uint32_t pad_h;        /* letterbox pad (top) */
    uint32_t pad_w;        /* letterbox pad (left) */
    uint32_t inv_scale_q16;/* (1/letterbox_scale) in Q16, for rescale */
    uint32_t in_offset;    /* absolute byte offset of int8 input [C,H,W] */
    uint32_t reserved0;
    uint32_t reserved1;
} ydev_header_t;

/* ── op-table entry: 8 x uint32 (32 bytes) ──
 *   w0: type(8) | stride(8) | pad(8)
 *   w1: in_a(16) | in_b(16)   (in_b=0xFFFF if unused)
 *   w2: out(16) | out_c(16)
 *   w3: in_c(16) | kh(8) | kw(8)
 *   w4: weight_offset (abs bytes, conv only)
 *   w5: bias_offset   (abs bytes, conv only; int64[out_c])
 *   w6: w_scale_q20   (conv only)
 *   w7: reserved
 */
typedef struct {
    uint32_t w0, w1, w2, w3, w_off, b_off, w_scale_q20, reserved;
} ydev_op_t;

/* decoded op */
typedef struct {
    uint8_t  type;
    uint8_t  stride;
    uint8_t  pad;
    int16_t  in_a;
    int16_t  in_b;
    uint16_t out;
    uint16_t out_c;
    uint16_t in_c;
    uint8_t  kh, kw;
    const int8_t  *weights;   /* [oc][ic][kh][kw] int8, or NULL */
    const int64_t *bias_q;    /* [oc] int64, or NULL */
    uint32_t w_scale_q20;
    int8_t  *dma_w;           /* pre-laid-out NC1HWC2 weight (resident), or NULL */
} yolo_op_t;

/* a runtime activation tensor: int8 [C][H][W] (CHW) + Q20 scale */
typedef struct {
    int8_t  *data;
    uint16_t c, h, w;
    uint32_t scale_q20;
} yolo_tensor_t;

#define YOLO_MAX_OPS    128
#define YOLO_MAX_TILES  32   /* conv0 (M=25600, tile_m<=1020) needs 26 tiles */
#define YOLO_MAX_DET     3
#define YOLO_MAX_K       1152 /* max im2col K = in_c*kh*kw across all convs */

typedef struct {
    ydev_header_t hdr;
    const uint8_t *base;      /* pointer to loaded .ydev image */

    uint16_t detect_idx[YOLO_MAX_DET];
    int32_t  anchors_q16[3][3][2];  /* grid units, Q16 */
    int32_t  strides[3];

    yolo_op_t ops[YOLO_MAX_OPS];
    uint32_t  n_ops;

    /* per-op result tensors (data buffers allocated on demand) */
    yolo_tensor_t results[YOLO_MAX_OPS];

    /* the quantized input tensor */
    yolo_tensor_t input;

    /* NPU DMA buffers (shared scratch, sized for the largest layer) */
    int8_t   *dma_input;      /* im2col NC1HWC2 feature */
    int8_t   *dma_output;     /* INT32 matmul output */
    uint64_t *dma_regs;       /* 108 * MAX_TILES */
    void     *dma_regcmd;
    void     *dma_tasks;

    int8_t   *im2col;         /* im2col scratch [K][M] as int8 rows */
} yolo_model_t;

/* ── API ── */
int  yolo_model_load(yolo_model_t *m, const void *ydev, uint32_t size);
void yolo_forward(yolo_model_t *m);
void yolo_postprocess(yolo_model_t *m, uint32_t conf_q16, uint32_t iou_q16);

/* conv via NPU (im2col + tiled matmul), returns INT32 acc into out32[oc*oh*ow]
 * laid out [oc][oh*ow]. */
int  yolo_conv_npu(yolo_model_t *m, const yolo_op_t *op,
                   const yolo_tensor_t *in, int32_t *out32,
                   int *out_h, int *out_w);

/* integer post-conv + tensor ops (yolo_ops.c) */
void yolo_conv_finish(yolo_model_t *m, const yolo_op_t *op, const int32_t *acc32,
                      int oh, int ow, uint32_t x_s);
void yolo_maxpool(yolo_model_t *m, const yolo_op_t *op, const yolo_tensor_t *in);
void yolo_upsample(yolo_model_t *m, const yolo_op_t *op, const yolo_tensor_t *in);
void yolo_concat(yolo_model_t *m, const yolo_op_t *op,
                 const yolo_tensor_t *a, const yolo_tensor_t *b);
void yolo_add(yolo_model_t *m, const yolo_op_t *op,
              const yolo_tensor_t *a, const yolo_tensor_t *b);

#endif /* YOLO_H */

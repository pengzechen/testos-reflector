#include "t_types.h"
#include "yolo/yolo.h"
#include "npu/rknpu.h"
#include "lib/t_logger.h"

/*
 * YOLOv5n INT8 object-detection test.
 *
 * The .ydev bundle (weights + quantized bus.jpg input, produced by
 * tools/yolo/export_device.py) is pre-loaded into memory by U-Boot TFTP before
 * the kernel runs, at YOLO_MODEL_ADDR.  It must sit above the 1 GB heap and
 * clear of the LLM model region (0x20000000).
 */

#define YOLO_MODEL_ADDR  0x21000000u        /* above LLM model/tokenizer region */
#define YOLO_MODEL_SIZE  (8u * 1024 * 1024) /* .ydev ~3.5MB (weights+input) */

/* conf=0.25, iou=0.45 in Q16 (match the reference sim) */
#define CONF_Q16  ((uint32_t)(0.25 * 65536))
#define IOU_Q16   ((uint32_t)(0.45 * 65536))

static yolo_model_t g_yolo;

void
rknpu_test_yolo(void)
{
    logger_info("========================================\n");
    logger_info("  YOLOv5n INT8 Detection Test\n");
    logger_info("  build: %s %s  (max_m cap build)\n", __DATE__, __TIME__);
    logger_info("========================================\n");

    logger_info("Loading model from 0x%x...\n", YOLO_MODEL_ADDR);
    int ret = yolo_model_load(&g_yolo,
                              (const void *)(uint64_t)YOLO_MODEL_ADDR,
                              YOLO_MODEL_SIZE);
    if (ret != 0) {
        logger_error("YOLO model load failed: %d\n", ret);
        return;
    }

    yolo_forward(&g_yolo);
    yolo_postprocess(&g_yolo, CONF_Q16, IOU_Q16);

    logger_info("========================================\n");
    logger_info("  YOLO Test Complete\n");
    logger_info("========================================\n");
}

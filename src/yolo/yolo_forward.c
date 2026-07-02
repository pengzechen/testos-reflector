#include "yolo/yolo.h"
#include "mem/t_mem.h"
#include "lib/t_logger.h"
#include "dev/t_timer.h"

/*
 * yolo_forward — walk the flattened op table, producing one result tensor per
 * op.  Conv ops run on the NPU (im2col + tiled matmul) and finish on the CPU;
 * maxpool / upsample / concat / add are pure integer CPU ops.
 *
 * Mirrors export_device.py::CModel.forward exactly.
 */

/* shared INT32 conv-accumulator scratch: max [oc][oh*ow] = 256*40*40 = 409600 */
static int32_t g_acc32[409600];

static const yolo_tensor_t *
input_tensor(yolo_model_t *m, int16_t idx)
{
    if (idx < 0)
        return &m->input;
    return &m->results[idx];
}

void
yolo_forward(yolo_model_t *m)
{
    extern uint64_t g_prof_im2col, g_prof_submit, g_prof_readback;
    g_prof_im2col = g_prof_submit = g_prof_readback = 0;
    uint64_t t0 = timer_get_system_ticks();

    for (uint32_t i = 0; i < m->n_ops; i++) {
        const yolo_op_t *op = &m->ops[i];
        const yolo_tensor_t *a = input_tensor(m, op->in_a);

        switch (op->type) {
        case YOP_CONV:
        case YOP_CONV_LIN: {
            int oh, ow;
            int ret = yolo_conv_npu(m, op, a, g_acc32, &oh, &ow);
            if (ret != 0) {
                logger_error("YOLO: conv op %d failed (%d)\n", i, ret);
                return;
            }
            yolo_conv_finish(m, op, g_acc32, oh, ow, a->scale_q20);
            break;
        }
        case YOP_MAXPOOL:
            yolo_maxpool(m, op, a);
            break;
        case YOP_UPSAMPLE:
            yolo_upsample(m, op, a);
            break;
        case YOP_CONCAT: {
            const yolo_tensor_t *b = input_tensor(m, op->in_b);
            yolo_concat(m, op, a, b);
            break;
        }
        case YOP_ADD: {
            const yolo_tensor_t *b = input_tensor(m, op->in_b);
            yolo_add(m, op, a, b);
            break;
        }
        default:
            logger_error("YOLO: unknown op type %d at %d\n", op->type, i);
            return;
        }

        yolo_tensor_t *r = &m->results[op->out];
        if ((i % 16) == 0 || i == m->n_ops - 1)
            logger_info("  op %2d t=%d -> [%d,%d,%d] s_q20=%d\n",
                        i, op->type, r->c, r->h, r->w, r->scale_q20);
    }

    uint64_t ms = (timer_get_system_ticks() - t0) * 1000 / TIMER_FREQUENCY_HZ;
    extern uint64_t g_prof_im2col, g_prof_submit, g_prof_readback;
    uint64_t im_ms = g_prof_im2col   * 1000 / TIMER_FREQUENCY_HZ;
    uint64_t su_ms = g_prof_submit   * 1000 / TIMER_FREQUENCY_HZ;
    uint64_t rb_ms = g_prof_readback * 1000 / TIMER_FREQUENCY_HZ;
    logger_info("YOLO: forward done in %llu ms "
                "(im2col=%llu submit=%llu readback=%llu finish/other=%llu)\n",
                ms, im_ms, su_ms, rb_ms,
                ms - im_ms - su_ms - rb_ms);
}

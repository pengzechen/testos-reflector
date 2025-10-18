

#include "rkconfig.h"
#include "rknpu.h"
#include "lib/t_logger.h"

#define RKNPU_JOB_PC       (1 << 0)
#define RKNPU_JOB_BLOCK    (0 << 1)
#define RKNPU_JOB_PINGPONG (1 << 2)


void
rknpu_test()
{
    // 准备task
    npu_task_t task;
    task.flags       = 0;
    task.op_idx      = 0;
    task.enable_mask = 0x1;
    task.int_mask    = 0x300;  // Wait for DPU to finish
    task.int_clear   = INT_CLEAR_VALUE;
    task.int_status  = 0;

    task.regcfg_amount = 0x0;  // TODO
    task.regcfg_offset = 0;
    task.regcmd_addr   = 0x0;  // TODO

    // 准备submit结构
    npu_submit_t submit;
    submit.flags        = RKNPU_JOB_PC | RKNPU_JOB_BLOCK | RKNPU_JOB_PINGPONG;
    submit.timeout      = 1000;  // 1秒超时
    submit.task_start   = 0;
    submit.task_number  = 1;
    submit.task_counter = 0;
    submit.priority     = 0;

    submit.task_obj_addr               = (uint64_t) (void *) &task;
    submit.task_base_addr              = (uint64_t) (void *) &task;
    submit.user_data                   = 0;
    submit.core_mask                   = 0x1;  // 使用核心0
    submit.fence_fd                    = -1;   // 不使用fence
    submit.subcore_task[0].task_start  = 0;
    submit.subcore_task[0].task_number = 1;


    rknpu_submit_task(&task,
                      &task,
                      submit.task_start,
                      submit.task_number,
                      submit.core_mask,
                      submit.timeout);
}
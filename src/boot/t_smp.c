
#include "t_types.h"
#include "t_psci.h"
#include "lib/t_logger.h"
#include "cfg/t_cfg.h"

extern void
_t_stack_top();
extern void
_t_stack_top_second();
extern void
t_second_entry();


void
start_secondary_cpus()
{
    logger_info("core 0 thread info addr: %llx\n",
           (struct thread_into *) (void *) (_t_stack_top - T_STACK_SIZE));

    for (int i = 1; i < T_SMP_NUM; i++) {
        int result = smc_call(PSCI_0_2_FN64_CPU_ON,
                              i,
                              (uint64_t) (void *) t_second_entry,
                              i);
        if (result != 0) {
            logger_error("smc_call failed!\n");
        }

        // 做一点休眠 保证第二个核 初始化完成
        for (int j = 0; j < 0xff; j++)
            for (int k = 0; k < 0xffff; k++)
                ;

        logger_info("core %d thread info addr: %llx\n",
               i,
               (void *) (_t_stack_top_second - T_STACK_SIZE * i));
    }
}

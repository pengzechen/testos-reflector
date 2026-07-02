
#include "t_types.h"

#include "dev/t_gicv3.h"
#include "dev/t_dw_uart.h"
#include "dev/t_timer.h"
#include "dev/xmodem_dw_uart.h"
#include "npu/rknpu.h"
#include "dev/cru.h"
#include "dev/scmi.h"
// #include "dev/pcie.h"

#include "t_sysreg.h"


#include "lib/t_logger.h"
#include "lib/rand.h"
#include "mem/t_mem.h"

#include "t_psci.h"
#include "cfg/t_cfg.h"
#include "mem/cache.h"

extern void
__bss_start();
extern void
__bss_end();
extern void
__heap_flag();

extern void
start_secondary_cpus();

// ============================= 核心抽象 =========================
// ===============================================================

#define MAX_CPUS T_SMP_NUM

typedef struct
{
    void (*entry)(int cpu_id, void *arg);  // 核心执行函数
    void        *arg;                      // 参数指针
    volatile int start_flag;               // =1 表示任务可执行
    volatile int done_flag;                // =1 表示任务完成
} cpu_task_t;

static cpu_task_t cpu_tasks[MAX_CPUS];
volatile int      cpu_task_ready[MAX_CPUS];
volatile int      cpu_task_done[MAX_CPUS];

volatile int cpu_online[MAX_CPUS];


// 为某个核心设置任务。
void
launch_on_core(int cpu_id, void (*entry)(int, void *), void *arg)
{
    cpu_tasks[cpu_id].entry      = entry;
    cpu_tasks[cpu_id].arg        = arg;
    cpu_tasks[cpu_id].done_flag  = 0;
    cpu_tasks[cpu_id].start_flag = 1;  // 唤醒该核
}

// 主核调用之后，所有副核开始执行task。
void
wake_all_cores(int num)
{
    (void) num;
    asm volatile("sev");
}

// 主核调用 wakeup 之后需要等待其它副核执行完成。
void
wait_all_cores(int num)
{
    (void) num;
    // 记录每个核是否已经报告过完成
    int reported[MAX_CPUS] = {0};

    for (int i = 1; i < MAX_CPUS; i++) {
        while (cpu_tasks[i].done_flag == 0)
            ;  // busy wait

        if (!reported[i]) {
            reported[i] = 1;
            logger_info("core %d finished its task\n", i);
        }
    }
}

// 副核在汇编设置一些寄存器就会跳到这里。循环等待任务
void
t_secondary_main(uint64_t cpu_id)
{
    logger_info("second core id: %d\n", cpu_id);
    // logger_warn("second core CurrentEL = %u\n", READ_CURRENTEL());
    cpu_online[cpu_id] = 1;
    DSB_SY();
    while (1) {
        if (cpu_tasks[cpu_id].start_flag) {
            void (*fn)(int, void *)      = cpu_tasks[cpu_id].entry;
            void *arg                    = cpu_tasks[cpu_id].arg;
            cpu_tasks[cpu_id].start_flag = 0;

            if (fn)
                fn(cpu_id, arg);

            cpu_tasks[cpu_id].done_flag = 1;
            DSB_SY();
        }
        // logger_info("core %d wait for event\n");
        asm volatile("wfe" ::: "memory");
    }
}

// XMODEM 接收缓冲区（全局变量，用于在命令间共享）
#define XMODEM_BUF_SIZE (2 * 1024 * 1024)  // 2MB 缓冲区
static uint8_t *g_xmodem_buf    = NULL;
static ssize_t  g_last_received = 0;

void
uart_test()
{
    logger_info("========================================\n");
    logger_info("UART Interrupt Test Started\n");
    logger_info("Press any key for echo test...\n");
    logger_info("========================================\n");

    uint64_t       last_print_tick        = timer_get_system_ticks();
    const uint64_t PRINT_INTERVAL         = TIMER_FREQUENCY_HZ * 5;  // 每 5 秒打印一次
    uint32_t       test_counter           = 0;
    bool           periodic_print_enabled = true;  // 控制周期性打印

    while (1) {
        uint64_t current_tick = timer_get_system_ticks();

        // 定期输出测试字符串
        if (periodic_print_enabled && current_tick - last_print_tick >= PRINT_INTERVAL) {
            last_print_tick = current_tick;
            test_counter++;
            logger_info("[UART Test #%u] Uptime: %llu seconds, Ticks: %llu\n",
                        test_counter,
                        current_tick / TIMER_FREQUENCY_HZ,
                        current_tick);
        }

        // 检查并回显键盘输入
        char c = dw_uart_getchar();
        // 回显字符
        // logger_info("Echo: '%c' (0x%02x, ASCII %d)\n",
        //            c >= 32 && c <= 126 ? c : '?',  // 只显示可打印字符
        //            (unsigned char)c,
        //            (unsigned char)c);

        // 特殊命令处理
        if (c == 'h' || c == 'H') {
            logger_info("\n=== UART Test Commands ===\n");
            logger_info("  h/H - Show this help\n");
            logger_info("  s/S - Show statistics\n");
            logger_info("  t/T - Show current time\n");
            logger_info("  p/P - Toggle periodic print\n");
            logger_info("  x/X - Start XMODEM file receive\n");
            logger_info("  d/D - Dump received file data\n");
            logger_info("  q/Q - Quit (return to WFI loop)\n");
            logger_info("==========================\n\n");
        } else if (c == 'd' || c == 'D') {
            // 显示已接收的文件数据
            if (g_xmodem_buf == NULL) {
                logger_warn("No file received yet. Use 'x' to receive a file first.\n");
            } else if (g_last_received <= 0) {
                logger_warn("No valid file data. Received size: %ld\n", g_last_received);
            } else {
                logger_info("\n=== Received File Data ===\n");
                logger_info("Buffer address: %p\n", g_xmodem_buf);
                logger_info("Total size: %ld bytes\n\n", g_last_received);

                // 显示前 256 字节（使用行缓冲，避免每个字节都加前缀）
                size_t display_len = g_last_received > 256 ? 256 : g_last_received;
                logger_info("First %ld bytes (hex):\n", display_len);

                char line[128];  // 行缓冲
                for (size_t i = 0; i < display_len; i++) {
                    if (i % 16 == 0) {
                        // 新行开始，输出地址
                        my_snprintf(line, sizeof(line), "%04lx: ", i);
                        dw_uart_putstr(line);
                    }
                    // 拼接十六进制字节
                    my_snprintf(line, sizeof(line), "%02x ", g_xmodem_buf[i]);
                    dw_uart_putstr(line);

                    if ((i + 1) % 16 == 0) {
                        // 行尾，输出 ASCII
                        dw_uart_putstr(" |");
                        for (size_t j = i - 15; j <= i; j++) {
                            char ch = g_xmodem_buf[j];
                            if (ch >= 32 && ch <= 126) {
                                char ascii[2] = {ch, '\0'};
                                dw_uart_putstr(ascii);
                            } else {
                                dw_uart_putstr(".");
                            }
                        }
                        dw_uart_putstr("|\n");
                    }
                }
                // 处理不完整的最后一行
                if (display_len % 16 != 0) {
                    size_t last_line_start = (display_len / 16) * 16;
                    size_t last_line_len   = display_len % 16;
                    // 填充空格
                    for (size_t j = 0; j < (16 - last_line_len) * 3; j++) {
                        dw_uart_putstr(" ");
                    }
                    dw_uart_putstr(" |");
                    for (size_t j = last_line_start; j < display_len; j++) {
                        char ch = g_xmodem_buf[j];
                        if (ch >= 32 && ch <= 126) {
                            char ascii[2] = {ch, '\0'};
                            dw_uart_putstr(ascii);
                        } else {
                            dw_uart_putstr(".");
                        }
                    }
                    dw_uart_putstr("|\n");
                }

                // 显示最后 256 字节
                if (g_last_received > 256) {
                    size_t start = g_last_received - 256;
                    logger_info("\nLast 256 bytes (hex):\n");
                    for (size_t i = start; i < (size_t) g_last_received; i++) {
                        if ((i - start) % 16 == 0) {
                            my_snprintf(line, sizeof(line), "%04lx: ", i);
                            dw_uart_putstr(line);
                        }
                        my_snprintf(line, sizeof(line), "%02x ", g_xmodem_buf[i]);
                        dw_uart_putstr(line);

                        if ((i - start + 1) % 16 == 0) {
                            dw_uart_putstr(" |");
                            for (size_t j = i - 15; j <= i; j++) {
                                char ch = g_xmodem_buf[j];
                                if (ch >= 32 && ch <= 126) {
                                    char ascii[2] = {ch, '\0'};
                                    dw_uart_putstr(ascii);
                                } else {
                                    dw_uart_putstr(".");
                                }
                            }
                            dw_uart_putstr("|\n");
                        }
                    }
                    // 处理最后一行
                    size_t last_bytes = (g_last_received - start) % 16;
                    if (last_bytes != 0) {
                        for (size_t j = 0; j < (16 - last_bytes) * 3; j++) {
                            dw_uart_putstr(" ");
                        }
                        dw_uart_putstr(" |");
                        size_t last_line_start = g_last_received - last_bytes;
                        for (size_t j = last_line_start; j < (size_t) g_last_received; j++) {
                            char ch = g_xmodem_buf[j];
                            if (ch >= 32 && ch <= 126) {
                                char ascii[2] = {ch, '\0'};
                                dw_uart_putstr(ascii);
                            } else {
                                dw_uart_putstr(".");
                            }
                        }
                        dw_uart_putstr("|\n");
                    }
                }

                logger_info("\n==========================\n\n");
            }
        } else if (c == 'x' || c == 'X') {
            // XMODEM 文件接收测试
            if (g_xmodem_buf == NULL) {
                g_xmodem_buf = (uint8_t *) t_mem_alloc(XMODEM_BUF_SIZE);
                if (g_xmodem_buf == NULL) {
                    logger_error("Failed to allocate XMODEM buffer\n");
                } else {
                    logger_info("Allocated XMODEM buffer at %p\n", g_xmodem_buf);
                }
            }

            if (g_xmodem_buf != NULL) {
                logger_info("\n=== Starting XMODEM-1K Receive ===\n");
                logger_info("Buffer size: %d bytes\n", XMODEM_BUF_SIZE);
                logger_info("Please start sending file using XMODEM-1K protocol...\n");
                logger_info("Example: sx -k yourfile.bin < /dev/ttyUSB0 > /dev/ttyUSB0\n");
                logger_info("===================================\n\n");

                // 禁用周期性打印，避免干扰传输
                bool old_periodic      = periodic_print_enabled;
                periodic_print_enabled = false;

                // 调用 xmodem 接收函数
                ssize_t received = xmodem_receive_1k(g_xmodem_buf, XMODEM_BUF_SIZE);

                // 保存接收结果
                g_last_received = received;

                // 恢复周期性打印
                periodic_print_enabled = old_periodic;

                if (received > 0) {
                    logger_info("\n=== XMODEM Receive SUCCESS ===\n");
                    logger_info("Received: %ld bytes\n", received);
                    logger_info("Buffer address: %p\n", g_xmodem_buf);
                    logger_info("Use 'd' command to view the data\n");
                    logger_info("==============================\n\n");
                } else if (received == 0) {
                    logger_warn("\n=== XMODEM Receive CANCELLED ===\n");
                    logger_warn("Transfer was cancelled by sender\n");
                    logger_warn("================================\n\n");
                } else {
                    logger_error("\n=== XMODEM Receive FAILED ===\n");
                    logger_error("Error code: %ld\n", received);
                    logger_error("=============================\n\n");
                }
            }
        } else if (c == 'p' || c == 'P') {
            periodic_print_enabled = !periodic_print_enabled;
            logger_info("Periodic print: %s\n", periodic_print_enabled ? "ENABLED" : "DISABLED");
        } else if (c == 's' || c == 'S') {
            uint32_t tx_irqs, rx_irqs, tx_usage, rx_usage;
            dw_uart_get_stats(&tx_irqs, &rx_irqs, &tx_usage, &rx_usage);
            bool     tx_int_enabled = dw_uart_is_tx_interrupt_enabled();
            uint32_t last_iir       = dw_uart_get_last_iir();
            uint32_t tx_sent        = dw_uart_get_tx_sent_total();

            logger_info("\n=== UART Statistics ===\n");
            logger_info("  TX Buffer Usage: %u/%d bytes\n", tx_usage, 1024);
            logger_info("  RX Buffer Usage: %u/%d bytes\n", rx_usage, 1024);
            logger_info("  TX Interrupts: %u\n", tx_irqs);
            logger_info("  TX Sent Bytes: %u\n", tx_sent);
            logger_info("  RX Interrupts: %u\n", rx_irqs);
            logger_info("  TX INT Enabled: %s\n", tx_int_enabled ? "YES" : "NO");
            logger_info("  Last IIR: 0x%x\n", last_iir);
            logger_info("  System Ticks: %llu\n", timer_get_system_ticks());
            logger_info("  Uptime: %llu seconds\n", timer_get_system_ticks() / TIMER_FREQUENCY_HZ);
            logger_info("=======================\n\n");
        } else if (c == 't' || c == 'T') {
            uint64_t uptime_ms = timer_get_uptime_ms();
            logger_info("\n=== Current Time ===\n");
            logger_info("  Uptime: %llu.%03llu seconds\n", uptime_ms / 1000, uptime_ms % 1000);
            logger_info("  Ticks: %llu\n", timer_get_system_ticks());
            logger_info("====================\n\n");
        } else if (c == 'q' || c == 'Q') {
            logger_info("Exiting UART test, entering WFI loop...\n");
            break;
        }
    }

    logger_info("UART test completed, entering idle loop\n");
}

// ============================== 首核 ============================



// 主内核入口函数
void
t_kernel_main(uint64_t id)
{
    // ========== 阶段 1: 早期初始化（无中断） ==========
    // 先初始化早期串口，这样后续的 logger_info 就能工作
    // dw_uart_early_init();

    logger_info("Compiled on %s at %s\n", __DATE__, __TIME__);

    logger_info("bss start: %p, end: %p, size: %u KB\n",
                &__bss_start,
                &__bss_end,
                ((uint64_t) &__bss_end - (uint64_t) &__bss_start) / 1024);

    logger_info("heap flag address: %p\n", &__heap_flag);

    logger_warn("CurrentEL = %u\n", READ_CURRENTEL());

    logger_info("smp: %d\n", T_SMP_NUM);

    logger_info("main core id: %d\n", id);

    init_cpu_cacheline_size();

    gicv3_init();


    timer_init();
    // timer_dump_info();

    // 启动多核
    start_secondary_cpus();

    {
        // 最多等待 5 秒让所有副核就绪；全部就绪则提前结束等待
        uint64_t freq        = timer_get_frequency();
        uint64_t start_ticks = CNTPCT_EL0_READ();
        uint64_t deadline    = start_ticks + 5ULL * freq;  // 5 秒超时

        int all_online = 0;
        while (CNTPCT_EL0_READ() < deadline) {
            all_online = 1;
            for (int i = 1; i < T_SMP_NUM; i++) {
                if (cpu_online[i] == 0) {
                    all_online = 0;
                    break;
                }
            }
            if (all_online)
                break;
            // 小幅让步，避免过度占用总线
            asm volatile("nop");
        }

        if (all_online) {
            logger_info("All %d secondary cores online within 5 seconds.\n", T_SMP_NUM - 1);
        } else {
            logger_warn("Timeout after 5 seconds: some secondary cores are not online.\n");
            for (int i = 1; i < T_SMP_NUM; i++) {
                if (cpu_online[i] == 0) {
                    logger_warn("  - core %d NOT online\n", i);
                }
            }
        }
    }


    // 启用定时器
    timer_enable();
    enable_interrupts();  // daifclr 2
    logger_info("After enabling interrupts\n");

    dw_uart_init();


    t_run_printf_tests();


    // 随机数模块测试
    srand_tick();

    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());
    logger_info("Random number test: %ld\n", rand_tick());

    // 申请内存测试
    size_t heap_size = (1 << 28);  // 1 G
    t_mem_init(heap_size);

#if 1
    void t_mem_run_tests(void);
    void t_mem_run_stress_tests(void);
    t_mem_run_tests();
    t_mem_run_stress_tests();
#endif

    // PCIe WiFi card init (RTL8852BE on pcie2x1l0)
    // pcie_init();

#if 1
    {
    extern void rknpu_test_matmul(void);
    extern void rknpu_test_conv2d(void);
    extern void rknpu_test_dwconv2d(void);
    extern void rknpu_test_tile_matmul(void);
    extern void rknpu_test_avgpool(void);
    extern void rknpu_test_maxpool(void);
    extern void rknpu_test_concat(void);
    extern void rknpu_test_eltwise_add(void);
    extern void rknpu_test_conv2d_bs(void);
    extern void rknpu_test_reshape(void);
    extern void rknpu_test_mul(void);
    extern void rknpu_test_sigmoid(void);
    extern void rknpu_test_softmax(void);
    extern void rknpu_test_llm(void);
    extern void rknpu_test_yolo(void);

    // RKNPU 初始化测试
    rknpu_init();

    // 测试
    rknpu_test_matmul();
    rknpu_test_conv2d();
    rknpu_test_dwconv2d();
    rknpu_test_tile_matmul();
    rknpu_test_avgpool();
    rknpu_test_maxpool();
    rknpu_test_concat();
    rknpu_test_eltwise_add();
    rknpu_test_conv2d_bs();
    rknpu_test_reshape();
    rknpu_test_mul();
    rknpu_test_sigmoid();
    rknpu_test_softmax();

    rknpu_test_yolo();

    // rknpu_test_llm();

    }
#endif


    while (1) {
        WFI();
    }
}

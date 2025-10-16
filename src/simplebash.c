/*
 * Simple Bash - A basic shell implementation for TestOS
 * 
 * This implements a simple command-line interface using UART
 * for input/output with interrupt-driven communication.
 */

#include "t_types.h"
#include "t_dw_uart.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"
#include "t_task.h"
#include "t_timer.h"

#define MAX_CMD_LEN 256
#define MAX_ARGS    16

// Command buffer and parsing
static char  cmd_buffer[MAX_CMD_LEN];
static int   cmd_pos = 0;
static char *args[MAX_ARGS];
static int   arg_count = 0;

// Shell prompt
static const char *prompt = "testos> ";

// Forward declarations
static void
shell_print_prompt(void);
static void
shell_process_char(char c);
static void
shell_execute_command(void);
static void
shell_parse_command(void);
static void
shell_clear_buffer(void);

// Built-in commands
static void
cmd_help(void);
static void
cmd_echo(void);
static void
cmd_clear(void);
static void
cmd_info(void);
static void
cmd_uart_status(void);
static void
cmd_ps(void);

// Command structure
typedef struct
{
    const char *name;
    void (*func)(void);
    const char *description;
} shell_command_t;

// Available commands
static const shell_command_t commands[] = {
    {"help", cmd_help, "Show this help message"},
    {"echo", cmd_echo, "Echo arguments"},
    {"clear", cmd_clear, "Clear screen"},
    {"info", cmd_info, "Show system information"},
    {"uart", cmd_uart_status, "Show UART status"},
    {"ps", cmd_ps, "Show running processes"},
    {NULL, NULL, NULL}  // Terminator
};

// Initialize the shell
void
simplebash_init(void)
{
    shell_clear_buffer();
    dw_uart_putstr("\r\n");
    dw_uart_putstr("=================================\r\n");
    dw_uart_putstr("  TestOS Simple Bash v1.0\r\n");
    dw_uart_putstr("  Type 'help' for commands\r\n");
    dw_uart_putstr("=================================\r\n");
    shell_print_prompt();
}

// Main shell loop - call this instead of WFI
void
simplebash_run(void)
{
    char c;

    // Check for incoming characters
    while (dw_uart_getchar_nb(&c)) {
        shell_process_char(c);
    }
}

// Print shell prompt
static void
shell_print_prompt(void)
{
    dw_uart_putstr(prompt);
    dw_uart_flush();
}

// Process incoming character
static void
shell_process_char(char c)
{
    switch (c) {
        case '\r':  // Carriage return
        case '\n':  // Line feed
            dw_uart_putstr("\r\n");
            if (cmd_pos > 0) {
                cmd_buffer[cmd_pos] = '\0';
                shell_execute_command();
            }
            shell_clear_buffer();
            shell_print_prompt();
            break;

        case '\b':  // Backspace
        case 0x7F:  // DEL
            if (cmd_pos > 0) {
                cmd_pos--;
                dw_uart_putstr("\b \b");  // Backspace, space, backspace
            }
            break;

        case 0x03:  // Ctrl+C
            dw_uart_putstr("^C\r\n");
            shell_clear_buffer();
            shell_print_prompt();
            break;

        default:
            // Regular character
            if (c >= 32 && c <= 126 && cmd_pos < MAX_CMD_LEN - 1) {
                cmd_buffer[cmd_pos++] = c;
                dw_uart_putchar(c);  // Echo character
            }
            break;
    }
}

// Clear command buffer
static void
shell_clear_buffer(void)
{
    cmd_pos   = 0;
    arg_count = 0;
    memset(cmd_buffer, 0, sizeof(cmd_buffer));
    memset(args, 0, sizeof(args));
}

// Parse command into arguments
static void
shell_parse_command(void)
{
    char *token = cmd_buffer;
    arg_count   = 0;

    // Skip leading spaces
    while (*token == ' ' || *token == '\t') {
        token++;
    }

    // Parse arguments separated by spaces
    while (*token && arg_count < MAX_ARGS - 1) {
        args[arg_count++] = token;

        // Find end of current argument
        while (*token && *token != ' ' && *token != '\t') {
            token++;
        }

        // Null-terminate current argument
        if (*token) {
            *token++ = '\0';

            // Skip spaces before next argument
            while (*token == ' ' || *token == '\t') {
                token++;
            }
        }
    }

    args[arg_count] = NULL;
}

// Execute parsed command
static void
shell_execute_command(void)
{
    shell_parse_command();

    if (arg_count == 0) {
        return;
    }

    // Look for built-in command
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(args[0], commands[i].name) == 0) {
            commands[i].func();
            return;
        }
    }

    // Command not found
    dw_uart_putstr("Command not found: ");
    dw_uart_putstr(args[0]);
    dw_uart_putstr("\r\nType 'help' for available commands.\r\n");
}

// Built-in command implementations
static void
cmd_help(void)
{
    dw_uart_putstr("Available commands:\r\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        dw_uart_putstr("  ");
        dw_uart_putstr(commands[i].name);
        dw_uart_putstr(" - ");
        dw_uart_putstr(commands[i].description);
        dw_uart_putstr("\r\n");
    }
}

static void
cmd_echo(void)
{
    for (int i = 1; i < arg_count; i++) {
        if (i > 1) {
            dw_uart_putchar(' ');
        }
        dw_uart_putstr(args[i]);
    }
    dw_uart_putstr("\r\n");
}

static void
cmd_clear(void)
{
    // Send ANSI escape sequence to clear screen
    dw_uart_putstr("\033[2J\033[H");
    dw_uart_putstr("TestOS Simple Bash v1.0\r\n");
}

static void
cmd_info(void)
{
    dw_uart_putstr("TestOS System Information:\r\n");
    dw_uart_putstr("  OS: TestOS\r\n");
    dw_uart_putstr("  Architecture: ARM64\r\n");
    dw_uart_putstr("  Shell: Simple Bash v1.0\r\n");
    dw_uart_putstr("  UART: PL011 with interrupt support\r\n");
}

static void
cmd_uart_status(void)
{
    dw_uart_putstr("UART Status:\r\n");
    dw_uart_putstr("  TX Buffer Usage: ");

    // Convert buffer usage to string (simple implementation)
    uint32_t usage = dw_uart_tx_buffer_usage();
    char     num_str[16];
    int      pos = 0;

    if (usage == 0) {
        num_str[pos++] = '0';
    } else {
        // Convert number to string (reverse order)
        uint32_t temp = usage;
        while (temp > 0) {
            num_str[pos++] = '0' + (temp % 10);
            temp /= 10;
        }

        // Reverse the string
        for (int i = 0; i < pos / 2; i++) {
            char tmp             = num_str[i];
            num_str[i]           = num_str[pos - 1 - i];
            num_str[pos - 1 - i] = tmp;
        }
    }

    num_str[pos] = '\0';
    dw_uart_putstr(num_str);
    dw_uart_putstr(" bytes\r\n");

    dw_uart_putstr("  RX Available: ");
    dw_uart_putstr(dw_uart_rx_available() ? "Yes" : "No");
    dw_uart_putstr("\r\n");
}

// 辅助函数：将数字转换为字符串
static void
uint_to_str(uint32_t num, char *str, int width)
{
    char temp[16];
    int  i = 0;

    if (num == 0) {
        temp[i++] = '0';
    } else {
        while (num > 0) {
            temp[i++] = '0' + (num % 10);
            num /= 10;
        }
    }

    // 反转字符串
    int j = 0;
    while (j < width - i) {
        str[j++] = ' ';  // 右对齐，左边填空格
    }
    while (i > 0) {
        str[j++] = temp[--i];
    }
    str[j] = '\0';
}

// 辅助函数：将64位数字转换为字符串
static void
uint64_to_str(uint64_t num, char *str, int width)
{
    char temp[32];
    int  i = 0;

    if (num == 0) {
        temp[i++] = '0';
    } else {
        while (num > 0) {
            temp[i++] = '0' + (num % 10);
            num /= 10;
        }
    }

    // 反转字符串
    int j = 0;
    while (j < width - i) {
        str[j++] = ' ';  // 右对齐，左边填空格
    }
    while (i > 0) {
        str[j++] = temp[--i];
    }
    str[j] = '\0';
}

// 辅助函数：获取任务状态字符串
static const char *
get_task_state_str(task_state_t state)
{
    switch (state) {
        case TASK_READY:      return "READY";
        case TASK_RUNNING:    return "RUN  ";
        case TASK_BLOCKED:    return "BLOCK";
        case TASK_SLEEPING:   return "SLEEP";
        case TASK_TERMINATED: return "TERM ";
        default:              return "UNK  ";
    }
}

// ps 命令实现
static void
cmd_ps(void)
{
    extern task_manager_t g_task_manager;

    dw_uart_putstr("Process Status:\r\n");
    dw_uart_putstr("PID  NAME         STATE CPU  TIME(ms) SLICE SLEEP_UNTIL\r\n");
    dw_uart_putstr("---- ------------ ----- --- -------- ----- -----------\r\n");

    uint64_t current_tick = timer_get_system_ticks();

    // 遍历所有任务
    for (int i = 0; i < MAX_TOTAL_TASKS; i++) {
        if (g_task_manager.task_used[i]) {
            task_t *task = &g_task_manager.task_pool[i];

            char pid_str[8];
            char cpu_str[8];
            char time_str[16];
            char slice_str[8];
            char sleep_str[16];

            // 格式化各个字段
            uint_to_str(task->task_id, pid_str, 4);
            uint_to_str(task->cpu_id, cpu_str, 3);
            uint64_to_str(task->total_runtime * 10, time_str, 8);  // 转换为毫秒
            uint_to_str(task->remaining_ticks, slice_str, 5);

            if (task->state == TASK_SLEEPING) {
                uint64_to_str(task->sleep_until_ticks, sleep_str, 11);
            } else {
                strcpy(sleep_str, "           ");  // 11个空格
            }

            // 输出任务信息
            dw_uart_putstr(pid_str);
            dw_uart_putstr(" ");

            // 任务名称（最多12个字符，左对齐）
            char name_padded[16];
            strncpy(name_padded, task->name, 12);
            name_padded[12] = '\0';
            int name_len = strlen(name_padded);
            dw_uart_putstr(name_padded);
            for (int j = name_len; j < 12; j++) {
                dw_uart_putchar(' ');
            }
            dw_uart_putstr(" ");

            dw_uart_putstr(get_task_state_str(task->state));
            dw_uart_putstr(" ");
            dw_uart_putstr(cpu_str);
            dw_uart_putstr(" ");
            dw_uart_putstr(time_str);
            dw_uart_putstr(" ");
            dw_uart_putstr(slice_str);
            dw_uart_putstr(" ");
            dw_uart_putstr(sleep_str);
            dw_uart_putstr("\r\n");
        }
    }

    // 显示当前系统信息
    dw_uart_putstr("\r\nSystem Info:\r\n");
    char tick_str[16];
    uint64_to_str(current_tick, tick_str, 10);
    dw_uart_putstr("Current Tick: ");
    dw_uart_putstr(tick_str);
    dw_uart_putstr("\r\n");

    char uptime_str[16];
    uint64_to_str(current_tick * 10, uptime_str, 10);  // 转换为毫秒
    dw_uart_putstr("Uptime (ms):  ");
    dw_uart_putstr(uptime_str);
    dw_uart_putstr("\r\n");
}

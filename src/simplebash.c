/*
 * Simple Bash - A basic shell implementation for TestOS
 * 
 * This implements a simple command-line interface using UART
 * for input/output with interrupt-driven communication.
 */

#include "t_types.h"
#include "t_uart.h"
#include "lib/t_string.h"
#include "lib/t_logger.h"

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
    {NULL, NULL, NULL}  // Terminator
};

// Initialize the shell
void
simplebash_init(void)
{
    shell_clear_buffer();
    uart_putstr("\r\n");
    uart_putstr("=================================\r\n");
    uart_putstr("  TestOS Simple Bash v1.0\r\n");
    uart_putstr("  Type 'help' for commands\r\n");
    uart_putstr("=================================\r\n");
    shell_print_prompt();
}

// Main shell loop - call this instead of WFI
void
simplebash_run(void)
{
    char c;

    // Check for incoming characters
    while (uart_getchar_nb(&c)) {
        shell_process_char(c);
    }
}

// Print shell prompt
static void
shell_print_prompt(void)
{
    uart_putstr(prompt);
    uart_flush();
}

// Process incoming character
static void
shell_process_char(char c)
{
    switch (c) {
        case '\r':  // Carriage return
        case '\n':  // Line feed
            uart_putstr("\r\n");
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
                uart_putstr("\b \b");  // Backspace, space, backspace
            }
            break;

        case 0x03:  // Ctrl+C
            uart_putstr("^C\r\n");
            shell_clear_buffer();
            shell_print_prompt();
            break;

        default:
            // Regular character
            if (c >= 32 && c <= 126 && cmd_pos < MAX_CMD_LEN - 1) {
                cmd_buffer[cmd_pos++] = c;
                uart_putchar(c);  // Echo character
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
    uart_putstr("Command not found: ");
    uart_putstr(args[0]);
    uart_putstr("\r\nType 'help' for available commands.\r\n");
}

// Built-in command implementations
static void
cmd_help(void)
{
    uart_putstr("Available commands:\r\n");
    for (int i = 0; commands[i].name != NULL; i++) {
        uart_putstr("  ");
        uart_putstr(commands[i].name);
        uart_putstr(" - ");
        uart_putstr(commands[i].description);
        uart_putstr("\r\n");
    }
}

static void
cmd_echo(void)
{
    for (int i = 1; i < arg_count; i++) {
        if (i > 1) {
            uart_putchar(' ');
        }
        uart_putstr(args[i]);
    }
    uart_putstr("\r\n");
}

static void
cmd_clear(void)
{
    // Send ANSI escape sequence to clear screen
    uart_putstr("\033[2J\033[H");
    uart_putstr("TestOS Simple Bash v1.0\r\n");
}

static void
cmd_info(void)
{
    uart_putstr("TestOS System Information:\r\n");
    uart_putstr("  OS: TestOS\r\n");
    uart_putstr("  Architecture: ARM64\r\n");
    uart_putstr("  Shell: Simple Bash v1.0\r\n");
    uart_putstr("  UART: PL011 with interrupt support\r\n");
}

static void
cmd_uart_status(void)
{
    uart_putstr("UART Status:\r\n");
    uart_putstr("  TX Buffer Usage: ");

    // Convert buffer usage to string (simple implementation)
    uint32_t usage = uart_tx_buffer_usage();
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
    uart_putstr(num_str);
    uart_putstr(" bytes\r\n");

    uart_putstr("  RX Available: ");
    uart_putstr(uart_rx_available() ? "Yes" : "No");
    uart_putstr("\r\n");
}

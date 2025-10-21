#include <stdio.h>
#include <stdint.h>

// 你的原函数
static inline uint32_t
rknpu_fuzz_status(uint32_t status)
{
    uint32_t fuzz_status = 0;

    if ((status & 0x3) != 0)
        fuzz_status |= 0x3;

    if ((status & 0xc) != 0)
        fuzz_status |= 0xc;

    if ((status & 0x30) != 0)
        fuzz_status |= 0x30;

    if ((status & 0xc0) != 0)
        fuzz_status |= 0xc0;

    if ((status & 0x300) != 0)
        fuzz_status |= 0x300;

    if ((status & 0xc00) != 0)
        fuzz_status |= 0xc00;

    return fuzz_status;
}

// 打印二进制辅助函数
void print_bin(uint32_t val, int bits)
{
    for (int i = bits - 1; i >= 0; i--) {
        printf("%c", (val & (1u << i)) ? '1' : '0');
        if (i % 4 == 0) printf(" ");
    }
}

// 主测试函数
int main(void)
{
    uint32_t tests[] = {
        0x300,   // 全 0
        0x100,   // bit0
    };

    int num_tests = sizeof(tests) / sizeof(tests[0]);

    printf("=== rknpu_fuzz_status 测试 ===\n");
    for (int i = 0; i < num_tests; i++) {
        uint32_t input = tests[i];
        uint32_t output = rknpu_fuzz_status(input);
        printf("[%d] input = 0x%03X (", i, input);
        print_bin(input, 12);
        printf(")  -->  output = 0x%03X (", output);
        print_bin(output, 12);
        printf(")\n");
    }

    return 0;
}

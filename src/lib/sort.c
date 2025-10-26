
#include "lib/sort.h"


static void
swap_bytes(char *a, char *b, size_t size)
{
    while (size--) {
        char tmp = *a;
        *a++     = *b;
        *b++     = tmp;
    }
}

void
qsort(void *base, size_t n, size_t size, cmp_func_t cmp)
{
    if (n < 2 || size == 0)
        return;

    char  *arr = (char *) base;
    size_t stack[64];  // 手动栈防止递归爆栈
    size_t top = 0;

    size_t left = 0, right = n - 1;

    // 压入初始范围
    stack[top++] = left;
    stack[top++] = right;

    while (top) {
        right = stack[--top];
        left  = stack[--top];

        if (left >= right)
            continue;

        // 选中枢值（中间元素）
        size_t i = left, j = right;
        char  *pivot = arr + ((left + right) / 2) * size;

        while (i <= j) {
            while (cmp(arr + i * size, pivot) < 0)
                i++;
            while (cmp(arr + j * size, pivot) > 0)
                j--;

            if (i <= j) {
                if (i != j)
                    swap_bytes(arr + i * size, arr + j * size, size);
                i++;
                if (j > 0)
                    j--;
            }
        }

        // 小区间压栈
        if (left < j) {
            stack[top++] = left;
            stack[top++] = j;
        }
        if (i < right) {
            stack[top++] = i;
            stack[top++] = right;
        }
    }
}
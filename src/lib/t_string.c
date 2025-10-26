/*
 * libc string functions
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License version 2.
 */

#include "lib/t_string.h"
#include "t_types.h"

unsigned long
strlen(const char *buf)
{
    unsigned long len = 0;

    while (*buf++)
        ++len;
    return len;
}

char *
strcat(char *dest, const char *src)
{
    char *p = dest;

    while (*p)
        ++p;
    while ((*p++ = *src++) != 0)
        ;
    return dest;
}

char *
strcpy(char *dest, const char *src)
{
    *dest = 0;
    return strcat(dest, src);
}

char *
strncpy(char *dest, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';

    return dest;
}

int
strncmp(const char *a, const char *b, size_t n)
{
    for (; n--; ++a, ++b)
        if (*a != *b || *a == '\0')
            return *a - *b;

    return 0;
}

int
strcmp(const char *a, const char *b)
{
    return strncmp(a, b, SIZE_MAX);
}

char *
strchr(const char *s, int c)
{
    while (*s != (char) c)
        if (*s++ == '\0')
            return NULL;
    return (char *) s;
}

char *
strstr(const char *s1, const char *s2)
{
    size_t l1, l2;

    l2 = strlen(s2);
    if (!l2)
        return (char *) s1;
    l1 = strlen(s1);
    while (l1 >= l2) {
        l1--;
        if (!memcmp(s1, s2, l2))
            return (char *) s1;
        s1++;
    }
    return NULL;
}

void *
memset(void *s, int c, size_t n)
{
    size_t i;
    char  *a = s;

    for (i = 0; i < n; ++i)
        a[i] = c;

    return s;
}

void *
memcpy(void *dest, const void *src, size_t n)
{
    size_t         i = 0;
    uint8_t       *d = (uint8_t *) dest;
    const uint8_t *s = (const uint8_t *) src;

    // --- 1. 逐字节拷贝直到对齐 2/4/8 ---
    while (i < n && ((uint64_t) (d + i) % 2 != 0 || (uint64_t) (s + i) % 2 != 0)) {
        d[i] = s[i];
        i++;
    }

    // --- 2. 8 字节拷贝 ---
    while (i + 7 < n && ((uint64_t) (d + i) % 8 == 0) && ((uint64_t) (s + i) % 8 == 0)) {
        *((uint64_t *) (d + i)) = *((uint64_t *) (s + i));
        i += 8;
    }

    // --- 3. 4 字节拷贝 ---
    while (i + 3 < n && ((uint64_t) (d + i) % 4 == 0) && ((uint64_t) (s + i) % 4 == 0)) {
        *((uint32_t *) (d + i)) = *((uint32_t *) (s + i));
        i += 4;
    }

    // --- 4. 2 字节拷贝 ---
    while (i + 1 < n && ((uint64_t) (d + i) % 2 == 0) && ((uint64_t) (s + i) % 2 == 0)) {
        *((uint16_t *) (d + i)) = *((uint16_t *) (s + i));
        i += 2;
    }

    // --- 5. 剩余逐字节拷贝 ---
    while (i < n) {
        d[i] = s[i];
        i++;
    }

    return dest;
}

void
memcpy_neon(uint8_t *dest, const uint8_t *src, size_t n)
{
    size_t i = 0;

    // --- 1. 对齐拷贝至 16 字节边界 ---
    while (i < n && ((uint64_t) (dest + i) % 16 != 0 || (uint64_t) (src + i) % 16 != 0)) {
        dest[i] = src[i];
        i++;
    }

    // --- 2. NEON 128-bit 拷贝 ---
    for (; i + 15 < n; i += 16) {
        __asm__ volatile("ld1 {v0.16b}, [%[src]]\n"   // 加载 16 字节到 NEON v0
                         "st1 {v0.16b}, [%[dest]]\n"  // 存储 16 字节到 dest
                         :
                         : [src] "r"(src + i), [dest] "r"(dest + i)
                         : "v0", "memory");
    }

    // --- 3. 剩余不足 16 字节拷贝 ---
    for (; i < n; i++) {
        dest[i] = src[i];
    }
}

int
memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = s1, *b = s2;
    int                  ret = 0;

    while (n--) {
        ret = *a - *b;
        if (ret)
            break;
        ++a, ++b;
    }
    return ret;
}

void *
memmove(void *dest, const void *src, size_t n)
{
    const unsigned char *s = src;
    unsigned char       *d = dest;

    if (d <= s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n, s += n;
        while (n--)
            *--d = *--s;
    }
    return dest;
}

void *
memchr(const void *s, int c, size_t n)
{
    const unsigned char *str = s, chr = (unsigned char) c;

    while (n--)
        if (*str++ == chr)
            return (void *) (str - 1);
    return NULL;
}

long
atol(const char *ptr)
{
    long        acc = 0;
    const char *s   = ptr;
    int         neg, c;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else {
        neg = 0;
        if (*s == '+')
            s++;
    }

    while (*s) {
        if (*s < '0' || *s > '9')
            break;
        c   = *s - '0';
        acc = acc * 10 + c;
        s++;
    }

    if (neg)
        acc = -acc;

    return acc;
}

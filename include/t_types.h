
#ifndef __TYPES_H__
#define __TYPES_H__

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef char      int8_t;
typedef short     int16_t;
typedef int       int32_t;
typedef long long int64_t;

typedef _Bool bool;
#define false 0
#define true  1

typedef unsigned long long size_t;
typedef unsigned long long vaddr_t;  // Virtual address type
typedef unsigned long long paddr_t;  // Physical address type

#define SIZE_MAX ((size_t) - 1)

#define NULL ((void *) 0)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define BIT(n) (1U << (n))

#endif
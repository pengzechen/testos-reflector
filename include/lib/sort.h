

#ifndef _QSORT_H
#define _QSORT_H

#include "t_types.h"
typedef int (*cmp_func_t)(const void *, const void *);


void
qsort(void *base, size_t n, size_t size, cmp_func_t cmp);

#endif  // _QSORT_H
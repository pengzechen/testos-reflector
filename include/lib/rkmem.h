
#ifndef RKMEM_H
#define RKMEM_H

#include "t_types.h"

extern void __heap_flag(void);

void rkmem_init(size_t heap_size);
void *rkmem_alloc(size_t size);
void rkmem_free(void *ptr); // 可选，简单实现可不释放

#endif /* RKMEM_H */
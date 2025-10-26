
#ifndef _REORDER_H
#define _REORDER_H

#include "t_types.h"

void
reorder_matrix_multi_core(int8_t         *dst,
                          const int8_t   *src,
                          const uint32_t *map,
                          size_t          total,
                          int             num_cores);

#endif  // _REORDER_H

#ifndef _REORDER_H
#define _REORDER_H

#include "t_types.h"

typedef struct {
    uint32_t dst_index;
    uint32_t src_index;
} reorder_entry_t;

void
reorder_matrix_multi_core(int8_t         *dst,
                          const int8_t   *src,
                          const uint32_t *map,
                          size_t          total,
                          int             num_cores);

void
reorder_matrix_multi_core_entries(int8_t                *dst,
                                  const int8_t          *src,
                                  const reorder_entry_t *entries,
                                  size_t                 total_entries,
                                  int                    num_cores);

#endif  // _REORDER_H
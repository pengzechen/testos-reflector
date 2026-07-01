#include "npu_reshape.h"
#include "lib/t_string.h"

void
reshape_int8(reshape_params_t *p)
{
    memcpy(p->output, p->input, p->total_bytes);
}

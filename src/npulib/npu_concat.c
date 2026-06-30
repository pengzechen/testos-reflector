#include "npu_concat.h"
#include "lib/t_string.h"

void
concat_channel_int8(concat_params_t *p)
{
    int total_c = 0;
    for (int i = 0; i < p->num_inputs; i++)
        total_c += p->inputs[i].channels;

    for (int y = 0; y < p->h; y++) {
        for (int x = 0; x < p->w; x++) {
            int c_offset = 0;
            for (int i = 0; i < p->num_inputs; i++) {
                int ch = p->inputs[i].channels;
                const int8_t *src = p->inputs[i].data
                    + (y * p->w + x) * ch;
                int8_t *dst = p->output
                    + (y * p->w + x) * total_c + c_offset;
                memcpy(dst, src, ch);
                c_offset += ch;
            }
        }
    }
}

#include "lib/rand.h"
#include "t_timer.h"

static uint32_t rand_seed = 0;

void srand_tick(void)
{
    uint64_t t = timer_get_system_ticks();
    // 将 tick 混合成 32bit 种子
    rand_seed ^= (uint32_t)t ^ (uint32_t)(t >> 32);
    rand_seed ^= (rand_seed << 13);
    rand_seed ^= (rand_seed >> 7);
    rand_seed ^= (rand_seed << 17);
}

uint32_t rand_tick(void)
{
    // 线性同余生成器
    rand_seed = rand_seed * 1664525u + 1013904223u;
    return rand_seed;
}
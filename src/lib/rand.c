#include "lib/rand.h"
#include "dev/t_timer.h"
#include "t_sysreg.h"

static uint32_t s[4];

static uint32_t rotl(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

static uint32_t splitmix32(uint32_t *z)
{
    *z += 0x9e3779b9u;
    uint32_t r = *z;
    r ^= r >> 16;
    r *= 0x85ebca6bu;
    r ^= r >> 13;
    r *= 0xc2b2ae35u;
    r ^= r >> 16;
    return r;
}

void srand_tick(void)
{
    uint64_t t = READ_CNTPCT_EL0();
    uint32_t z = (uint32_t)t ^ (uint32_t)(t >> 32);
    s[0] = splitmix32(&z);
    s[1] = splitmix32(&z);
    s[2] = splitmix32(&z);
    s[3] = splitmix32(&z);
    if ((s[0] | s[1] | s[2] | s[3]) == 0)
        s[0] = 1;
}

/* xoshiro128** */
uint32_t rand_tick(void)
{
    uint32_t result = rotl(s[1] * 5, 7) * 9;
    uint32_t t = s[1] << 9;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 11);
    return result;
}

#ifndef __MMIO_H__
#define __MMIO_H__

#include "t_types.h"
#include "t_sysreg.h"

static inline uint8_t
read8(const volatile void *addr)
{
    return *(const volatile uint8_t *) addr;
}

static inline void
write8(uint8_t value, volatile void *addr)
{
    *(volatile uint8_t *) addr = value;
}

static inline uint16_t
read16(const volatile void *addr)
{
    return *(const volatile uint16_t *) addr;
}

static inline void
write16(uint16_t value, volatile void *addr)
{
    *(volatile uint16_t *) addr = value;
}


static inline uint32_t
read32(const volatile void *addr)
{
    DSB_SY();
    return *(const volatile uint32_t *) addr;
    DSB_SY();
}

static inline void
write32(uint32_t value, volatile void *addr)
{
    DSB_SY();
    *(volatile uint32_t *) addr = value;
    DSB_SY();
}

static inline uint64_t
read64(const volatile void *addr)
{
    return *(const volatile uint64_t *) addr;
}

static inline void
write64(uint64_t value, volatile void *addr)
{
    *(volatile uint64_t *) addr = value;
}

#endif  // __MMIO_H__
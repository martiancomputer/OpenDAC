#ifndef OPENDAC_RING_MATH_H
#define OPENDAC_RING_MATH_H
#include "project.h"
#include <stdint.h>
#include <stdbool.h>

static inline uint32_t ring_dma_delta(uint32_t current, uint32_t previous)
{
    return (current + DMA_RING_HALFWORDS - previous) % DMA_RING_HALFWORDS;
}

static inline bool ring_can_write(uint32_t occupancy, uint32_t words,
                                  uint32_t guard)
{
    return occupancy <= DMA_RING_HALFWORDS &&
           words <= DMA_RING_HALFWORDS - occupancy &&
           guard < DMA_RING_HALFWORDS - occupancy - words;
}

#endif

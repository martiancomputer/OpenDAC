#include "ring_math.h"
#include <assert.h>

int main(void)
{
    assert(ring_dma_delta(10U, 5U) == 5U);
    assert(ring_dma_delta(4U, DMA_RING_HALFWORDS - 4U) == 8U);
    assert(ring_dma_delta(0U, 0U) == 0U);
    assert(ring_can_write(0U, 388U, DMA_SAFE_HALFWORDS));
    assert(!ring_can_write(DMA_RING_HALFWORDS - 4U, 4U, 0U));
    assert(!ring_can_write(DMA_RING_HALFWORDS + 1U, 4U, 0U));
    assert(!ring_can_write(0U, UINT32_MAX, 0U));
    uint32_t produced = UINT32_MAX - 3U;
    uint32_t consumed = produced - 100U;
    produced += 4U; // monotonic modulo-2^32 counters remain valid
    assert(produced - consumed == 104U);
    return 0;
}

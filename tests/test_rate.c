#include "rate_control.h"
#include "project.h"
#include <assert.h>

static void measure(uint32_t hz, uint16_t n, uint16_t div,
                    uint32_t actual, uint16_t expected_n,
                    uint16_t expected_div)
{
    rate_begin(hz, n, div);
    for (uint32_t sof = 0; sof < SOF_MEASUREMENT_COUNT; ++sof) {
        const uint32_t consumed = (uint32_t)
            (((uint64_t)actual * sof * DMA_HALFWORDS_PER_FRAME) / 1000U);
        rate_sof(consumed);
    }
    assert(rate_phase() == RATE_ADJUST_PENDING);
    rate_override_t correction = {0};
    assert(rate_take_adjustment(&correction));
    assert(correction.pll_n == expected_n);
    assert(correction.divisor == expected_div);
    assert(!rate_take_adjustment(&correction));
}

int main(void)
{
    measure(48000U, 384U, 25U, 18752U, 393U, 10U);
    measure(96000U, 424U, 23U, 37509U, 425U, 9U);
    assert(rate_override_for(48000U).divisor == 10U);
    assert(rate_override_for(96000U).divisor == 9U);
    rate_begin(48000U, 393U, 10U);
    assert(rate_phase() == RATE_VERIFYING);
    for (uint32_t sof = 0; sof < SOF_MEASUREMENT_COUNT; ++sof)
        rate_sof((uint32_t)(((uint64_t)47978U * sof * 4U) / 1000U));
    assert(rate_phase() == RATE_DONE);
    return 0;
}

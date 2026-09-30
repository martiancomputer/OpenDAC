#include "rate_control.h"
#include "project.h"

typedef struct {
    uint32_t hz;
    rate_phase_t phase;
    uint32_t first_halfwords;
    uint16_t sof_count;
    uint16_t pll_n;
    uint16_t divisor;
    uint32_t measured_hz;
} measurement_t;

static measurement_t current;
static rate_override_t saved_48;
static rate_override_t saved_96;

rate_override_t rate_override_for(uint32_t hz)
{
    if (hz == 48000U) return saved_48;
    if (hz == 96000U) return saved_96;
    return (rate_override_t){0};
}

void rate_begin(uint32_t hz, uint16_t pll_n, uint16_t divisor)
{
    rate_override_t saved = rate_override_for(hz);
    current = (measurement_t){
        .hz = hz, .phase = saved.valid ? RATE_VERIFYING : RATE_MEASURING,
        .pll_n = pll_n, .divisor = divisor
    };
}

void rate_sof(uint32_t consumed_halfwords)
{
    if (current.phase != RATE_MEASURING &&
        current.phase != RATE_VERIFYING) return;
    if (current.sof_count++ == 0U) {
        current.first_halfwords = consumed_halfwords;
        return;
    }
    if (current.sof_count < SOF_MEASUREMENT_COUNT) return;
    const uint32_t delta = consumed_halfwords - current.first_halfwords;
    current.measured_hz =
        (uint32_t)(((uint64_t)delta * 1000U) /
                   ((uint32_t)(current.sof_count - 1U) *
                    DMA_HALFWORDS_PER_FRAME));
    if (current.phase == RATE_VERIFYING) {
        const uint32_t low = current.hz * 975U / 1000U;
        const uint32_t high = current.hz * 1025U / 1000U;
        current.phase = (current.measured_hz >= low &&
                         current.measured_hz <= high) ? RATE_DONE : RATE_FAILED;
    } else {
        current.phase = RATE_ADJUST_PENDING;
    }
}

bool rate_take_adjustment(rate_override_t *out)
{
    if (current.phase != RATE_ADJUST_PENDING) return false;
    current.phase = RATE_FAILED;
    if (current.measured_hz >= current.hz * 975U / 1000U &&
        current.measured_hz <= current.hz * 1025U / 1000U) {
        current.phase = RATE_DONE;
        return false;
    }
    if (current.measured_hz < 12000U ||
        current.measured_hz > current.hz * 2U ||
        current.divisor < 4U || current.divisor > 511U ||
        current.pll_n < 192U || current.pll_n > 432U) return false;

    const uint32_t div = (uint32_t)(
        ((uint64_t)current.divisor * current.measured_hz +
         current.hz / 2U) / current.hz);
    if (div < 4U || div > 511U) return false;
    const uint64_t den = (uint64_t)current.measured_hz * current.divisor;
    const uint32_t n = (uint32_t)(
        ((uint64_t)current.pll_n * current.hz * div + den / 2U) / den);
    if (n < 192U || n > 432U) return false;

    *out = (rate_override_t){.pll_n = (uint16_t)n,
                             .divisor = (uint16_t)div, .valid = true};
    if (current.hz == 48000U) saved_48 = *out;
    else if (current.hz == 96000U) saved_96 = *out;
    else return false;
    return true;
}

rate_phase_t rate_phase(void) { return current.phase; }
uint32_t rate_measured_hz(void) { return current.measured_hz; }

uint32_t rate_feedback_q14(uint32_t hz, uint32_t queued_halfwords)
{
    // Asynchronous feedback handles small ring drift after the physical
    // I2S rate has been measured. It cannot mask the 0.3907 clock-scale fault.
    const int32_t wanted = DMA_RING_HALFWORDS / (2 * DMA_HALFWORDS_PER_FRAME);
    const int32_t writable = (int32_t)(
        (DMA_RING_HALFWORDS - queued_halfwords) / DMA_HALFWORDS_PER_FRAME);
    const int32_t deviation = writable - wanted;
    const int32_t trim = (int32_t)(((int64_t)hz * deviation) / 16384);
    int32_t feedback_hz = (int32_t)hz + trim;
    if (feedback_hz < (int32_t)hz - 1000) feedback_hz = (int32_t)hz - 1000;
    if (feedback_hz > (int32_t)hz + 1000) feedback_hz = (int32_t)hz + 1000;
    return (uint32_t)feedback_hz << 14;
}

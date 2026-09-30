#include "diag.h"
#include "audio_engine.h"
#include "rate_control.h"
#include "project.h"
#include "stm32f4xx_hal.h"
#include <string.h>

enum { EVENT_COUNT = 128, PAGE_EVENTS = 3 };
typedef struct {
    uint32_t tick;
    uint32_t kind;
    uint32_t a;
    uint32_t b;
} event_t;

diag_counters_t diag_counts = {
    .queue_min = UINT32_MAX,
    .feedback_min_q14 = UINT32_MAX
};
static event_t events[EVENT_COUNT];
static volatile uint32_t next_event;
static uint32_t status_words[16];
static uint32_t page_words[16];
static uint32_t extended_words[16];

void diag_event(diag_event_t kind, uint32_t a, uint32_t b)
{
    // Callers use the same USB/DMA IRQ priority. Main-loop callers briefly
    // mask interrupts so an event is published as a complete four-word unit.
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t slot = next_event++ & (EVENT_COUNT - 1U);
    events[slot] = (event_t){HAL_GetTick(), (uint32_t)kind, a, b};
    __set_PRIMASK(primask);
}

uint8_t *diag_status(uint16_t *length)
{
    // Read-only 64-byte EP0 snapshot, compatible with the diagnostic
    // transport described in research.md. Every field uses explicit units.
    status_words[0] = 0x31445541U; // AUD1
    status_words[1] = stream_rate();
    status_words[2] = ((uint32_t)audio_clock_n() << 16) |
                      audio_clock_divisor();
    status_words[3] = DMA_RING_HALFWORDS;
    status_words[4] = stream_queued_halfwords();
    status_words[5] = diag_counts.malformed;
    status_words[6] = diag_counts.dropped;
    status_words[7] = diag_counts.underruns;
    status_words[8] = diag_counts.overruns;
    status_words[9] = diag_counts.recoveries;
    status_words[10] = diag_counts.start_errors;
    status_words[11] = diag_counts.dma_errors;
    status_words[12] = (uint32_t)(((uint64_t)stream_rate() * 16384U +
                                  500U) / 1000U);
    status_words[13] = stream_feedback_q14();
    status_words[14] = diag_counts.usb_frames;
    status_words[15] = ((uint32_t)rate_phase() << 24) |
                       (rate_measured_hz() & 0xFFFFFFU);
    *length = sizeof(status_words);
    return (uint8_t *)status_words;
}

uint8_t *diag_page(uint16_t page, uint16_t *length)
{
    const uint32_t first = page * PAGE_EVENTS;
    const uint32_t available = next_event < EVENT_COUNT ? next_event : EVENT_COUNT;
    memset(page_words, 0, sizeof(page_words));
    page_words[0] = 0x31545645U; // EVT1
    page_words[1] = next_event;
    page_words[2] = first;
    if (first < available) {
        uint32_t count = available - first;
        if (count > PAGE_EVENTS) count = PAGE_EVENTS;
        page_words[3] = count;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t seq = next_event - available + first + i;
            memcpy(&page_words[4U + 4U * i],
                   &events[seq & (EVENT_COUNT - 1U)], sizeof(event_t));
        }
    }
    *length = sizeof(page_words);
    return (uint8_t *)page_words;
}

uint8_t *diag_extended(uint16_t page, uint16_t *length)
{
    memset(extended_words, 0, sizeof(extended_words));
    extended_words[0] = 0x32445541U; // AUD2
    extended_words[1] = page;
    if (page == 0U) {
        extended_words[2] = stream_requested_rate();
        extended_words[3] = stream_rate();
        extended_words[4] = rate_measured_hz();
        extended_words[5] = (uint32_t)rate_error_ppm();
        extended_words[6] = audio_clock_n();
        extended_words[7] = audio_clock_divisor() >> 1U;
        extended_words[8] = audio_clock_divisor() & 1U;
        extended_words[9] = (rate_calibration_valid(stream_rate()) ? 3U : 0U) |
                            (rate_gross_failure() ? 4U : 0U);
        extended_words[10] = (uint32_t)rate_phase();
        extended_words[11] = stream_state_code();
        extended_words[12] = audio_dac_is_muted() ? 1U : 0U;
        extended_words[13] = audio_amp_control_enabled() ?
                             (audio_amp_is_muted() ? 1U : 2U) : 0U;
        extended_words[14] = stream_queued_halfwords();
        extended_words[15] = stream_produced_halfwords();
    } else if (page == 1U) {
        extended_words[2] = stream_consumed_halfwords();
        extended_words[3] = diag_counts.overruns;
        extended_words[4] = diag_counts.underruns;
        extended_words[5] = diag_counts.dropped;
        extended_words[6] = diag_counts.dma_errors;
        extended_words[7] = diag_counts.start_errors;
        extended_words[8] = diag_counts.rate_changes;
        extended_words[9] = diag_counts.calibrations;
        extended_words[10] = diag_counts.calibration_failures;
        extended_words[11] = diag_counts.recoveries;
        extended_words[12] = stream_feedback_q14();
        extended_words[13] = diag_counts.dma_half;
        extended_words[14] = diag_counts.dma_full;
        extended_words[15] = diag_counts.usb_incomplete;
    } else if (page == 2U) {
        extended_words[2] = diag_counts.queue_min;
        extended_words[3] = diag_counts.queue_max;
        extended_words[4] = diag_counts.feedback_min_q14;
        extended_words[5] = diag_counts.feedback_max_q14;
        extended_words[6] = diag_counts.usb_frames;
        extended_words[7] = diag_counts.malformed;
        extended_words[8] = next_event;
    } else return NULL;
    *length = sizeof(extended_words);
    return (uint8_t *)extended_words;
}

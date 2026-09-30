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

diag_counters_t diag_counts;
static event_t events[EVENT_COUNT];
static volatile uint32_t next_event;
static uint32_t status_words[16];
static uint32_t page_words[16];

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
    status_words[12] = stream_rate() << 14;
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

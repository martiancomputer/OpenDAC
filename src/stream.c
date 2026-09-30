#include "audio_engine.h"
#include "rate_control.h"
#include "project.h"
#include "diag.h"
#include "stm32f4xx_hal.h"
#include <string.h>

typedef enum { STREAM_STOPPED, STREAM_PREFILL, STREAM_RUNNING,
               STREAM_FAULT } stream_state_t;

static uint16_t samples[DMA_RING_HALFWORDS];
static volatile uint32_t produced;
static volatile uint32_t consumed;
static volatile uint32_t last_dma_position;
static volatile uint32_t queued;
static volatile uint32_t feedback_q14;
static volatile uint32_t selected_rate = 96000U;
static volatile uint32_t pending_rate;
static volatile bool want_active;
static volatile stream_state_t state;
static uint16_t sof_trace_count;
static volatile bool software_mute;
static volatile uint8_t attenuation_steps;

void stream_set_controls(bool mute, int16_t volume_db256)
{
    software_mute = mute;
    if (volume_db256 > 0) volume_db256 = 0;
    if (volume_db256 < -24576) volume_db256 = -24576;
    attenuation_steps = (uint8_t)((-volume_db256 + 384) / 768);
}

static int32_t decode_pcm24(const uint8_t *p)
{
    int32_t value = (int32_t)((uint32_t)p[0] |
                    ((uint32_t)p[1] << 8U) |
                    ((uint32_t)p[2] << 16U));
    if (p[2] & 0x80U) value |= (int32_t)0xFF000000U;
    if (software_mute) return 0;
    const uint8_t steps = attenuation_steps;
    if (steps & 1U) value = (value * 181) / 256; // ~3 dB
    return value / (int32_t)(1U << (steps / 2U));
}

static void sample_dma_cursor(void)
{
    if (state != STREAM_RUNNING) return;
    const uint32_t remaining = audio_dma_remaining();
    if (remaining > DMA_RING_HALFWORDS) {
        state = STREAM_FAULT;
        audio_request_fault();
        diag_event(DIAG_DMA_ERROR, remaining, DMA_RING_HALFWORDS);
        return;
    }
    uint32_t position = DMA_RING_HALFWORDS - remaining;
    if (position == DMA_RING_HALFWORDS) position = 0U;
    const uint32_t moved =
        (position + DMA_RING_HALFWORDS - last_dma_position) %
        DMA_RING_HALFWORDS;
    last_dma_position = position;
    consumed += moved;
    queued = produced - consumed;
    if (queued > DMA_RING_HALFWORDS ||
        queued <= DMA_HALFWORDS_PER_FRAME) {
        ++diag_counts.underruns;
        diag_event(DIAG_UNDERRUN, queued, remaining);
        state = STREAM_FAULT;
    }
}

static void clear_stopped_buffer(uint32_t hz)
{
    // Only main calls this, after DMA has stopped; never clear live DMA data.
    memset(samples, 0, sizeof(samples));
    produced = 0U;
    consumed = 0U;
    last_dma_position = 0U;
    queued = 0U;
    selected_rate = hz;
    feedback_q14 = hz << 14;
    state = STREAM_PREFILL;
}

void stream_reset(uint32_t hz)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    state = STREAM_STOPPED;
    __set_PRIMASK(primask);
    if (!audio_stop_dma()) {
        state = STREAM_FAULT;
        audio_request_fault();
        return;
    }
    if (!audio_select_rate(hz)) {
        state = STREAM_FAULT;
        ++diag_counts.start_errors;
        diag_event(DIAG_START_ERROR, hz, 0U);
        return;
    }
    clear_stopped_buffer(hz);
    rate_begin(hz, audio_clock_n(), audio_clock_divisor());
    audio_clear_fault();
    diag_event(DIAG_RATE_CHANGE, hz,
               ((uint32_t)audio_clock_n() << 16) | audio_clock_divisor());
}

void stream_request_rate(uint32_t hz) { pending_rate = hz; }
void stream_set_active(bool active)
{
    if (active && !want_active) pending_rate = selected_rate;
    want_active = active;
}

bool stream_receive(const uint8_t *packet, uint16_t bytes)
{
    if (bytes > USB_PACKET_BYTES || bytes % USB_BYTES_PER_FRAME != 0U) {
        ++diag_counts.malformed;
        diag_event(DIAG_BAD_PACKET, bytes, diag_counts.malformed);
        return false;
    }
    if (!want_active || (state != STREAM_PREFILL &&
                         state != STREAM_RUNNING)) return false;
    sample_dma_cursor();
    if (state == STREAM_FAULT) return false;

    const uint32_t frames = bytes / USB_BYTES_PER_FRAME;
    const uint32_t words = frames * DMA_HALFWORDS_PER_FRAME;
    const uint32_t reserve = state == STREAM_RUNNING ?
                             DMA_SAFE_HALFWORDS : 0U;
    if (queued + words + reserve >= DMA_RING_HALFWORDS) {
        ++diag_counts.overruns;
        ++diag_counts.dropped;
        diag_event(DIAG_OVERRUN, queued, words);
        // The measured 34 ms restart storm came from treating producer
        // pressure as hardware failure. Leave I2S and PLLI2S running.
        return false;
    }
    uint32_t cursor = produced % DMA_RING_HALFWORDS;
    for (uint32_t i = 0; i < frames; ++i) {
        const uint8_t *frame = packet + i * USB_BYTES_PER_FRAME;
        for (uint32_t ch = 0; ch < 2U; ++ch) {
            const uint8_t *pcm = frame + ch * 3U;
            const uint32_t word = (uint32_t)decode_pcm24(pcm);
            samples[cursor++] = (uint16_t)(word >> 8U);
            samples[cursor++] = (uint16_t)(word << 8U);
            if (cursor == DMA_RING_HALFWORDS) cursor = 0U;
        }
    }
    produced += words;
    queued = produced - consumed;
    diag_counts.usb_frames += frames;
    return true;
}

void stream_sof(void)
{
    sample_dma_cursor();
    if (state == STREAM_RUNNING) {
        rate_sof(consumed);
        if (++sof_trace_count == 250U) {
            sof_trace_count = 0U;
            diag_event(DIAG_DMA, consumed, produced);
            diag_event(DIAG_BUFFER, queued, feedback_q14);
        }
    }
    feedback_q14 = rate_feedback_q14(selected_rate, queued);
}

void stream_service(void)
{
    uint32_t next;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    next = pending_rate;
    pending_rate = 0U;
    __set_PRIMASK(primask);

    if (next != 0U) stream_reset(next);

    if (!want_active) {
        if (state == STREAM_RUNNING) {
            state = STREAM_STOPPED;
            if (!audio_stop_dma()) audio_request_fault();
        }
        return;
    }
    if (audio_fault_pending() || state == STREAM_FAULT) {
        if (!audio_stop_dma()) return;
        ++diag_counts.recoveries;
        diag_event(DIAG_RESTART, diag_counts.recoveries, queued);
        // Genuine DMA/consumer fault: stop once and refill. An empty
        // producer queue while the host is idle cannot start a restart loop.
        clear_stopped_buffer(selected_rate);
        rate_begin(selected_rate, audio_clock_n(), audio_clock_divisor());
        audio_clear_fault();
    }

    rate_override_t correction;
    if (state == STREAM_RUNNING && rate_phase() == RATE_ADJUST_PENDING) {
        const uint32_t measured = rate_measured_hz();
        diag_event(DIAG_CLOCK_MEASURE, measured, selected_rate);
        if (rate_take_adjustment(&correction)) {
            diag_event(DIAG_CLOCK_TUNE, measured,
                       ((uint32_t)correction.pll_n << 16) |
                       correction.divisor);
            stream_reset(selected_rate); // one main-loop tuning transition
            return;
        }
    }
    if (state == STREAM_PREFILL && queued >= DMA_PREFILL_HALFWORDS) {
        if (!audio_start_dma(samples, DMA_RING_HALFWORDS)) {
            ++diag_counts.start_errors;
            state = STREAM_FAULT;
            audio_request_fault();
            diag_event(DIAG_START_ERROR, selected_rate,
                       diag_counts.start_errors);
            return;
        }
        // DMA starts at ring index zero, with at least half a ring of valid
        // frames. Keep all cursor accounting in DMA halfwords.
        last_dma_position = 0U;
        consumed = 0U;
        state = STREAM_RUNNING;
        audio_set_mute(false);
    }
}

void stream_dma_half(void) { ++diag_counts.dma_half; }
void stream_dma_full(void) { ++diag_counts.dma_full; }
uint32_t stream_rate(void) { return selected_rate; }
uint32_t stream_feedback_q14(void) { return feedback_q14; }
uint32_t stream_queued_halfwords(void) { return queued; }
uint32_t stream_consumed_halfwords(void) { return consumed; }

#include "audio_engine.h"
#include "rate_control.h"
#include "project.h"
#include "diag.h"
#include "stm32f4xx_hal.h"
#include "hardware.h"
#include "pcm24.h"
#include "ring_math.h"
#include <string.h>

typedef enum { STREAM_STOPPED, STREAM_PREFILL, STREAM_MEASURING,
               STREAM_SETTLING, STREAM_RUNNING, STREAM_FAULT,
               STREAM_CLOCK_FAILED } stream_state_t;

static uint16_t samples[DMA_RING_HALFWORDS];
static volatile uint32_t produced;
static volatile uint32_t consumed;
static volatile uint32_t last_dma_position;
static volatile uint32_t queued;
static volatile uint32_t feedback_q14;
static volatile uint32_t selected_rate = 96000U;
static volatile uint32_t requested_rate = 96000U;
static volatile uint32_t pending_rate;
static volatile bool want_active;
static volatile stream_state_t state;
static uint16_t sof_trace_count;
static volatile bool software_mute;
static volatile uint8_t attenuation_steps;
static uint32_t settle_tick;
static uint8_t consecutive_recoveries;

static bool dma_active(void)
{ return state == STREAM_MEASURING || state == STREAM_SETTLING ||
         state == STREAM_RUNNING; }

void stream_set_controls(bool mute, int16_t volume_db256)
{
    software_mute = mute;
    if (volume_db256 > 0) volume_db256 = 0;
    if (volume_db256 < -24576) volume_db256 = -24576;
    attenuation_steps = (uint8_t)((-volume_db256 + 384) / 768);
}

static void sample_dma_cursor(void)
{
    if (!dma_active()) return;
    const uint32_t remaining = audio_dma_remaining();
    if (remaining > DMA_RING_HALFWORDS) {
        state = STREAM_FAULT;
        audio_request_fault();
        diag_event(DIAG_DMA_ERROR, remaining, DMA_RING_HALFWORDS);
        return;
    }
    uint32_t position = DMA_RING_HALFWORDS - remaining;
    if (position == DMA_RING_HALFWORDS) position = 0U;
    const uint32_t moved = ring_dma_delta(position, last_dma_position);
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
    feedback_q14 = (uint32_t)(((uint64_t)hz * 16384U + 500U) / 1000U);
    state = STREAM_PREFILL;
}

void stream_reset(uint32_t hz)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    state = STREAM_STOPPED;
    __set_PRIMASK(primask);
    audio_output_mute();
    if (audio_amp_control_enabled())
        diag_event(DIAG_AMP_MUTED, hz, 0U);
    diag_event(DIAG_DAC_MUTED, hz, 0U);
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
    ++diag_counts.rate_changes;
    diag_event(DIAG_RATE_CHANGE, hz,
               ((uint32_t)audio_clock_n() << 16) | audio_clock_divisor());
}

void stream_request_rate(uint32_t hz)
{
    requested_rate = hz;
    pending_rate = hz;
    diag_event(DIAG_RATE_REQUEST, hz, selected_rate);
}
void stream_set_active(bool active)
{
    if (active && !want_active && pending_rate == 0U)
        pending_rate = selected_rate;
    want_active = active;
}

bool stream_receive(const uint8_t *packet, uint16_t bytes)
{
    if (bytes > USB_PACKET_BYTES || bytes % USB_BYTES_PER_FRAME != 0U) {
        ++diag_counts.malformed;
        diag_event(DIAG_BAD_PACKET, bytes, diag_counts.malformed);
        return false;
    }
    if (!want_active || pending_rate != 0U ||
        (state != STREAM_PREFILL && !dma_active()))
        return false;
    sample_dma_cursor();
    if (state == STREAM_FAULT) return false;

    const uint32_t frames = bytes / USB_BYTES_PER_FRAME;
    const uint32_t words = frames * DMA_HALFWORDS_PER_FRAME;
    const uint32_t reserve = dma_active() ?
                             DMA_SAFE_HALFWORDS : 0U;
    if (!ring_can_write(queued, words, reserve)) {
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
        uint16_t converted[DMA_HALFWORDS_PER_FRAME];
        pcm24_stereo_to_i2s(frame, converted, software_mute,
                            attenuation_steps);
        for (uint32_t word = 0; word < DMA_HALFWORDS_PER_FRAME; ++word) {
            samples[cursor++] = converted[word];
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
    if (dma_active()) {
        if (queued < diag_counts.queue_min)
            diag_counts.queue_min = queued;
        if (queued > diag_counts.queue_max)
            diag_counts.queue_max = queued;
        if (state == STREAM_MEASURING)
            rate_sof(consumed);
        if (++sof_trace_count == 250U) {
            sof_trace_count = 0U;
            diag_event(DIAG_DMA, consumed, produced);
            diag_event(DIAG_BUFFER, queued, feedback_q14);
        }
    }
    feedback_q14 = rate_feedback_q14(selected_rate, queued);
    if (feedback_q14 < diag_counts.feedback_min_q14)
        diag_counts.feedback_min_q14 = feedback_q14;
    if (feedback_q14 > diag_counts.feedback_max_q14)
        diag_counts.feedback_max_q14 = feedback_q14;
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
        if (state != STREAM_STOPPED) {
            const bool was_dma_active = dma_active();
            state = STREAM_STOPPED;
            if (was_dma_active && !audio_stop_dma()) audio_request_fault();
            memset(samples, 0, sizeof(samples));
            produced = consumed = queued = 0U;
        }
        if (!audio_dac_is_muted() || !audio_amp_is_muted())
            audio_output_mute();
        return;
    }
    if (audio_fault_pending() || state == STREAM_FAULT) {
        audio_output_mute();
        if (++consecutive_recoveries > 3U) {
            state = STREAM_CLOCK_FAILED;
            ++diag_counts.calibration_failures;
            diag_event(DIAG_RATE_FAILED, selected_rate,
                       consecutive_recoveries);
            (void)audio_stop_dma();
            return;
        }
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
    if (state == STREAM_MEASURING && rate_phase() == RATE_ADJUST_PENDING) {
        const uint32_t measured = rate_measured_hz();
        diag_event(DIAG_CLOCK_MEASURE, measured, selected_rate);
        if (rate_take_adjustment(&correction)) {
            ++diag_counts.calibrations;
            diag_event(DIAG_CLOCK_TUNE, measured,
                       ((uint32_t)correction.pll_n << 16) |
                       correction.divisor);
            stream_reset(selected_rate); // one main-loop tuning transition
            return;
        }
    }
    if (state == STREAM_MEASURING && rate_phase() == RATE_FAILED) {
        audio_output_mute();
        state = STREAM_CLOCK_FAILED;
        (void)audio_stop_dma();
        ++diag_counts.calibration_failures;
        diag_event(DIAG_RATE_FAILED, rate_measured_hz(),
                   (uint32_t)rate_error_ppm());
        return;
    }
    if (state == STREAM_MEASURING && rate_phase() == RATE_DONE) {
        diag_event(DIAG_RATE_VERIFY, rate_measured_hz(),
                   (uint32_t)rate_error_ppm());
        audio_dac_unmute();
        diag_event(DIAG_DAC_UNMUTED, selected_rate, 0U);
        settle_tick = HAL_GetTick();
        state = STREAM_SETTLING;
    }
    if (state == STREAM_SETTLING &&
        (uint32_t)(HAL_GetTick() - settle_tick) >= ANALOG_SETTLE_MS) {
        if (audio_amp_control_enabled()) {
            audio_amp_unmute();
            diag_event(DIAG_AMP_UNMUTED, selected_rate, 0U);
        }
        state = STREAM_RUNNING;
        consecutive_recoveries = 0U;
        diag_event(DIAG_RATE_COMPLETE, selected_rate,
                   rate_measured_hz());
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
        state = STREAM_MEASURING;
        diag_event(DIAG_DMA_STARTED, selected_rate, queued);
    }
}

void stream_dma_half(void) { ++diag_counts.dma_half; }
void stream_dma_full(void) { ++diag_counts.dma_full; }
uint32_t stream_rate(void) { return selected_rate; }
uint32_t stream_feedback_q14(void) { return feedback_q14; }
uint32_t stream_queued_halfwords(void) { return queued; }
uint32_t stream_consumed_halfwords(void) { return consumed; }
uint32_t stream_produced_halfwords(void) { return produced; }
uint32_t stream_state_code(void) { return (uint32_t)state; }
uint32_t stream_requested_rate(void) { return requested_rate; }
void stream_quiesce(void)
{
    want_active = false;
    state = STREAM_STOPPED;
    audio_output_mute();
    (void)audio_stop_dma();
}

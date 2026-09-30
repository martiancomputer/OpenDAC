#ifndef OPENDAC_AUDIO_ENGINE_H
#define OPENDAC_AUDIO_ENGINE_H
#include <stdint.h>
#include <stdbool.h>

bool audio_hw_init(void);
bool audio_select_rate(uint32_t hz);
bool audio_start_dma(uint16_t *data, uint16_t halfwords);
bool audio_stop_dma(void);
uint32_t audio_dma_remaining(void);
uint16_t audio_clock_n(void);
uint16_t audio_clock_divisor(void);
void audio_set_mute(bool mute);
void audio_output_mute(void);
void audio_dac_unmute(void);
void audio_amp_unmute(void);
bool audio_dac_is_muted(void);
bool audio_amp_is_muted(void);
bool audio_amp_control_enabled(void);
void audio_request_fault(void);
bool audio_fault_pending(void);
void audio_clear_fault(void);

void stream_reset(uint32_t hz);
void stream_request_rate(uint32_t hz);
void stream_set_active(bool active);
void stream_set_controls(bool mute, int16_t volume_db256);
bool stream_receive(const uint8_t *packet, uint16_t bytes);
void stream_sof(void);
void stream_service(void);
void stream_dma_half(void);
void stream_dma_full(void);
uint32_t stream_rate(void);
uint32_t stream_feedback_q14(void);
uint32_t stream_queued_halfwords(void);
uint32_t stream_consumed_halfwords(void);
uint32_t stream_produced_halfwords(void);
uint32_t stream_state_code(void);
uint32_t stream_requested_rate(void);
void stream_quiesce(void);

#endif

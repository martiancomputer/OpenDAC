#ifndef OPENDAC_DIAG_H
#define OPENDAC_DIAG_H
#include <stdint.h>
typedef enum {
    DIAG_RX = 1, DIAG_DMA, DIAG_BUFFER, DIAG_BAD_PACKET,
    DIAG_OVERRUN, DIAG_UNDERRUN, DIAG_DMA_ERROR, DIAG_CLOCK_MEASURE,
    DIAG_CLOCK_TUNE, DIAG_CLOCK_VERIFY, DIAG_RATE_CHANGE, DIAG_START_ERROR,
    DIAG_RESTART, DIAG_USB_INCOMPLETE, DIAG_RATE_REQUEST,
    DIAG_DAC_MUTED, DIAG_AMP_MUTED, DIAG_DMA_STARTED,
    DIAG_RATE_VERIFY, DIAG_DAC_UNMUTED, DIAG_AMP_UNMUTED,
    DIAG_RATE_FAILED, DIAG_RATE_COMPLETE
} diag_event_t;

typedef struct {
    volatile uint32_t malformed;
    volatile uint32_t dropped;
    volatile uint32_t underruns;
    volatile uint32_t overruns;
    volatile uint32_t recoveries;
    volatile uint32_t start_errors;
    volatile uint32_t dma_errors;
    volatile uint32_t usb_frames;
    volatile uint32_t dma_half;
    volatile uint32_t dma_full;
    volatile uint32_t usb_incomplete;
    volatile uint32_t rate_changes;
    volatile uint32_t calibrations;
    volatile uint32_t calibration_failures;
    volatile uint32_t queue_min;
    volatile uint32_t queue_max;
    volatile uint32_t feedback_min_q14;
    volatile uint32_t feedback_max_q14;
} diag_counters_t;

extern diag_counters_t diag_counts;
void diag_event(diag_event_t kind, uint32_t a, uint32_t b);
uint8_t *diag_status(uint16_t *length);
uint8_t *diag_page(uint16_t page, uint16_t *length);
uint8_t *diag_extended(uint16_t page, uint16_t *length);
#endif

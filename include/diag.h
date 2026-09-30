#ifndef OPENDAC_DIAG_H
#define OPENDAC_DIAG_H
#include <stdint.h>
typedef enum {
    DIAG_RX = 1, DIAG_DMA, DIAG_BUFFER, DIAG_BAD_PACKET,
    DIAG_OVERRUN, DIAG_UNDERRUN, DIAG_DMA_ERROR, DIAG_CLOCK_MEASURE,
    DIAG_CLOCK_TUNE, DIAG_CLOCK_VERIFY, DIAG_RATE_CHANGE, DIAG_START_ERROR,
    DIAG_RESTART, DIAG_USB_INCOMPLETE
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
} diag_counters_t;

extern diag_counters_t diag_counts;
void diag_event(diag_event_t kind, uint32_t a, uint32_t b);
uint8_t *diag_status(uint16_t *length);
uint8_t *diag_page(uint16_t page, uint16_t *length);
#endif

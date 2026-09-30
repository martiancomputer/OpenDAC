#ifndef OPENDAC_PROJECT_H
#define OPENDAC_PROJECT_H

#include <stdint.h>

enum {
    USB_AUDIO_OUT = 0x01,
    USB_FEEDBACK_IN = 0x81,
    AUDIO_INTERFACE = 1,
    DFU_INTERFACE = 2,
    USB_BYTES_PER_FRAME = 6,
    DMA_HALFWORDS_PER_FRAME = 4,
    MAX_RATE_HZ = 96000,
    MAX_PACKET_FRAMES = MAX_RATE_HZ / 1000 + 1,
    USB_PACKET_BYTES = MAX_PACKET_FRAMES * USB_BYTES_PER_FRAME,
    DMA_RING_HALFWORDS = MAX_PACKET_FRAMES * DMA_HALFWORDS_PER_FRAME * 12,
    DMA_SAFE_HALFWORDS = 2 * MAX_PACKET_FRAMES * DMA_HALFWORDS_PER_FRAME,
    DMA_PREFILL_HALFWORDS = DMA_RING_HALFWORDS / 2,
    SOF_MEASUREMENT_COUNT = 750
};

_Static_assert(USB_PACKET_BYTES == 582, "USB full-speed packet capacity");
_Static_assert(DMA_RING_HALFWORDS == 4656, "measured ring size");
_Static_assert(DMA_RING_HALFWORDS % (2 * DMA_HALFWORDS_PER_FRAME) == 0,
               "DMA callbacks must align with frames");
_Static_assert(DMA_SAFE_HALFWORDS + USB_PACKET_BYTES / USB_BYTES_PER_FRAME *
               DMA_HALFWORDS_PER_FRAME < DMA_RING_HALFWORDS / 2,
               "guard and packet fit in half the ring");

#endif

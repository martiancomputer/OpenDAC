#include "pcm24.h"

int32_t pcm24_decode(const uint8_t bytes[3])
{
    uint32_t word = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
                    ((uint32_t)bytes[2] << 16U);
    if ((bytes[2] & 0x80U) != 0U) word |= 0xFF000000U;
    return (int32_t)word;
}

void pcm24_stereo_to_i2s(const uint8_t frame[6], uint16_t out[4],
                         bool mute, uint8_t attenuation_steps)
{
    for (uint32_t ch = 0; ch < 2U; ++ch) {
        int32_t value = mute ? 0 : pcm24_decode(&frame[ch * 3U]);
        if ((attenuation_steps & 1U) != 0U)
            value = value * 181 / 256; // half of a 6 dB attenuation pair
        value /= (int32_t)(1U << (attenuation_steps / 2U));
        // +20% amplitude. Compute in 32 bits (24-bit input * 6 fits), then
        // saturate to the signed 24-bit DAC range rather than wrapping.
        value = value * PCM_GAIN_NUMERATOR / PCM_GAIN_DENOMINATOR;
        if (value > 8388607) value = 8388607;
        if (value < -8388608) value = -8388608;
        const uint32_t word = (uint32_t)value;
        out[ch * 2U] = (uint16_t)(word >> 8U);
        out[ch * 2U + 1U] = (uint16_t)(word << 8U);
    }
}

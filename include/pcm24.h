#ifndef OPENDAC_PCM24_H
#define OPENDAC_PCM24_H
#include <stdint.h>
#include <stdbool.h>

enum { PCM_GAIN_NUMERATOR = 6, PCM_GAIN_DENOMINATOR = 5 };

int32_t pcm24_decode(const uint8_t bytes[3]);
void pcm24_stereo_to_i2s(const uint8_t frame[6], uint16_t out[4],
                         bool mute, uint8_t attenuation_steps);
#endif

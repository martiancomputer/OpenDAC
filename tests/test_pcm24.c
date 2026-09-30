#include "pcm24.h"
#include <assert.h>

int main(void)
{
    assert(pcm24_decode((const uint8_t[]){0, 0, 0}) == 0);
    assert(pcm24_decode((const uint8_t[]){255, 255, 127}) == 8388607);
    assert(pcm24_decode((const uint8_t[]){0, 0, 128}) == -8388608);
    assert(pcm24_decode((const uint8_t[]){255, 255, 255}) == -1);
    const uint8_t stereo[6] = {0, 0, 64, 0, 0, 192};
    uint16_t out[4] = {0};
    pcm24_stereo_to_i2s(stereo, out, false, 0);
    assert(out[0] == 0x4CCC && out[1] == 0xCC00);
    assert(out[2] == 0xB333 && out[3] == 0x3400);
    const uint8_t full_scale[6] = {255, 255, 127, 0, 0, 128};
    pcm24_stereo_to_i2s(full_scale, out, false, 0);
    assert(out[0] == 0x7FFF && out[1] == 0xFF00);
    assert(out[2] == 0x8000 && out[3] == 0);
    pcm24_stereo_to_i2s(stereo, out, false, 2);
    assert(out[0] == 0x2666 && out[1] == 0x6600);
    assert(out[2] == 0xD999 && out[3] == 0x9A00);
    pcm24_stereo_to_i2s(stereo, out, true, 0);
    for (unsigned i = 0; i < 4; ++i) assert(out[i] == 0);
    return 0;
}

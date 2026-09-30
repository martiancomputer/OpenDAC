# OpenDAC

STM32F401CC USB Audio Class 1 sink firmware for a PCM5102A DAC. This is a
fresh, compact implementation informed by the local working `ref/` tree and
the experiments recorded in the local `research.md`; those private reference
files are not part of the distributable source tree. The TPA6138A2 is an
analog output stage; its optional MUTE GPIO is disabled until the board is
electrically validated.

The device advertises packed 24-bit stereo PCM at 44.1, 48, and 96 kHz, a USB
asynchronous feedback endpoint, volume/mute, and a DFU runtime interface.
All rates on this reimplementation still require physical validation.
The clock controller measures I²S DMA consumption over 750 USB SOFs,
computes one bounded correction per rate, then verifies it. The ring buffer
and USB staging buffers are static; the application does not allocate memory.
The feedback endpoint sends samples per USB millisecond in 10.14 format;
diagnostic feedback values use those same on-wire units.
The PCM conversion stage applies a fixed 1.2× digital amplitude boost
(approximately +1.6 dB) after USB volume attenuation, saturating at 24-bit
full scale. Peaks above about 83% of full scale can therefore clip; turn down
the host volume if distortion appears.

| USB stream | Firmware | Hardware status |
| --- | --- | --- |
| S24_3LE stereo, 44.1 kHz | Implemented with separate measured calibration | Unvalidated |
| S24_3LE stereo, 48 kHz | Implemented with measured calibration | Known-good in earlier prototype; reimplementation unvalidated |
| S24_3LE stereo, 96 kHz | Implemented with measured calibration | Known-good in earlier prototype; reimplementation unvalidated |
| Native S16, 192 kHz, DSD, multichannel | Unsupported | Not advertised |

The DAC stays muted through the 750-SOF clock measurement and any one-time
correction. It unmutes only after rate verification; the optional headphone
stage enables 20 ms later. A failed rate remains muted. USB mute/volume is
separate from the physical transition mute.

## Build

Install `arm-none-eabi-gcc`, `arm-none-eabi-objcopy`, `make`, Python 3, and a
native C compiler. The hardware clock configuration assumes a 25 MHz HSE.

```sh
git submodule update --init --depth 1 vendor/STM32CubeF4
git -C vendor/STM32CubeF4 submodule update --init --depth 1 \
  Drivers/CMSIS/Device/ST/STM32F4xx Drivers/STM32F4xx_HAL_Driver
make -j4
make test
```

The official ST dependency is a Git submodule at `vendor/STM32CubeF4`, pinned
to release tag `v1.28.3`. ST's own repository contains nested submodules for
the HAL and CMSIS device headers; initialize those two for this build.
Do not edit files under `vendor/`; change the pinned revision deliberately.

`build/opendac.bin` is the flash image. `make flash-dfu` is available only
when the board is already in STM32 ROM DFU mode and `dfu-util` is installed.
Flashing and hardware enumeration have **not** been performed for this
reimplementation. The VID/PID `0483:5730` is for development compatibility
with the reference firmware; use a properly assigned VID/PID for distribution.

## Wiring and limits

| Signal | F401 pin | Destination |
| --- | --- | --- |
| I²S LRCK | PB12 | PCM5102A LRCK |
| I²S BCK | PB13 | PCM5102A BCK |
| I²S DIN | PB15 | PCM5102A DIN |
| DAC mute | PB8 | PCM5102A XSMT, if connected |
| Optional amp mute | PB9 | TPA6138A2 MUTE, disabled by default |
| USB FS | PA11/PA12 | D-/D+ |

PCM5102A is clocked in its three-wire I²S mode; no MCLK is emitted. The
analog supply, output amplifier, level shifting, grounding, and charge pumps
must be reviewed against the actual board schematic before connecting
headphones. A successful USB enumeration does not prove analog-stage safety.
If PB9 is enabled in `include/hardware.h`, fit an external pulldown so the
amplifier remains muted while the MCU is held in reset or running ROM DFU.

See `src/stream.c` for queue ownership, `src/rate_control.c` for clock
measurement, `src/usb_audio.c` for descriptors and class requests, and
`src/dfu.c` for the detach-to-ROM sequence. Diagnostic vendor requests are
IN `0x5A` (legacy 64-byte status), `0x5B` (64-byte event page selected by
`wIndex`), and `0x5C` (extended status pages 0/1/2). The new page 0 contains
requested/active/measured rate, signed ppm error, clock fields, calibration
phase, audio state, mute states and ring position. Page 1 contains counters;
page 2 contains soak-test queue and feedback extrema.
Page 0 word 9 uses bit 0 for a saved per-rate calibration, bit 1 for that
calibration applied to the active clock, and bit 2 for a gross verification
failure (over 10,000 ppm). All words are little-endian 32-bit values.
Page 0 word 13 is `0` when amplifier GPIO control is disabled, `1` when
controlled and muted, or `2` when controlled and enabled; `0` makes no claim
about the physical amplifier's mute state.
These are development interfaces, not a stable public protocol.

For Linux host-side rate selection and verification, see
`docs/linux-rate-switching.md`. ROM DFU detach first mutes both analog stages
and stops DMA before disconnecting USB.

## Provenance and licensing

The design was informed by the GPL-3.0 project
[STM32F411_USB_AUDIO_DAC](https://github.com/har-in-air/STM32F411_USB_AUDIO_DAC)
and the local patch/reference tree. The application source here is released
under GPL-3.0; see `LICENSE` and `NOTICE`. STM32CubeF4 is maintained by ST
and retains its own license terms inside the submodule.

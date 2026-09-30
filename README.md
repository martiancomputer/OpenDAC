# OpenDAC

STM32F401CC USB Audio Class 1 sink firmware for a PCM5102A DAC. This is a
fresh, compact implementation informed by the local working `ref/` tree and
the experiments recorded in the local `research.md`; those private reference
files are not part of the distributable source tree. The TPA6138A2 is an
analog output stage and is not configured by this firmware.

The device advertises packed 24-bit stereo PCM at 48 and 96 kHz, a USB
asynchronous feedback endpoint, volume/mute, and a DFU runtime interface.
44.1 kHz is deliberately not advertised because it has not been validated.
The clock controller measures I²S DMA consumption over 750 USB SOFs,
computes one bounded correction per rate, then verifies it. The ring buffer
and USB staging buffers are static; the application does not allocate memory.

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
| USB FS | PA11/PA12 | D-/D+ |

PCM5102A is clocked in its three-wire I²S mode; no MCLK is emitted. The
analog supply, output amplifier, level shifting, grounding, and charge pumps
must be reviewed against the actual board schematic before connecting
headphones. A successful USB enumeration does not prove analog-stage safety.

See `src/stream.c` for queue ownership, `src/rate_control.c` for clock
measurement, `src/usb_audio.c` for descriptors and class requests, and
`src/dfu.c` for the detach-to-ROM sequence. Diagnostic vendor requests are
IN `0x5A` (64-byte status) and `0x5B` (64-byte event page selected by
`wIndex`). They are development interfaces, not a stable public protocol.

## Provenance and licensing

The design was informed by the GPL-3.0 project
[STM32F411_USB_AUDIO_DAC](https://github.com/har-in-air/STM32F411_USB_AUDIO_DAC)
and the local patch/reference tree. The application source here is released
under GPL-3.0; see `LICENSE` and `NOTICE`. STM32CubeF4 is maintained by ST
and retains its own license terms inside the submodule.

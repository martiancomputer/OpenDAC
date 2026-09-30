# Linux rate selection and verification

The device accepts the USB stream rate selected by the host. It cannot infer
the original WAV/FLAC rate. A player's decoded source rate, PipeWire graph
rate, and ALSA USB hardware rate may differ.

First identify the card with `aplay -l`. While playback is active, inspect
the negotiated USB rate and format:

```sh
cat /proc/asound/cardX/pcm0p/sub0/hw_params
```

Replace `X` with the card number. `S24_3LE` and `rate: 44100`, `48000`, or
`96000` indicate the actual USB stream. A 16-bit source may correctly be
converted by the host to S24_3LE; that does not imply a native S16 alternate
setting in this firmware.

For a direct ALSA smoke test, stop other users of the device and run one of:

```sh
speaker-test -D hw:CARD=OpenDAC,DEV=0 -c 2 -F S24_3LE -r 44100
speaker-test -D hw:CARD=OpenDAC,DEV=0 -c 2 -F S24_3LE -r 48000
speaker-test -D hw:CARD=OpenDAC,DEV=0 -c 2 -F S24_3LE -r 96000
```

Use the card identifier reported by `aplay -l` if `OpenDAC` differs. PipeWire
may need the device released before direct `hw:` access. Do not assume that
changing a media player's source file switches the hardware rate: PipeWire
can resample to its current graph rate. Verify `hw_params` during every test.

After programming a new image, test enumeration, each rate, repeated rate
changes, DFU detach, and 30–60 minute playback at each rate. Capture the
device's diagnostic counters and measured-rate/ppm pages before claiming a
rate validated. In particular, 44.1 kHz has not yet passed physical testing.

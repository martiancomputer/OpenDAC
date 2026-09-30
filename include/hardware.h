#ifndef OPENDAC_HARDWARE_H
#define OPENDAC_HARDWARE_H

// Board pin map. PB9 is reserved for a future amplifier MUTE connection;
// enable only after checking the assembled schematic and hardware pulldown.
#define CONFIG_TPA6138A2 0
#define PCM_XSMT_PORT GPIOB
#define PCM_XSMT_PIN GPIO_PIN_8
#define TPA_MUTE_PORT GPIOB
#define TPA_MUTE_PIN GPIO_PIN_9
#define ANALOG_SETTLE_MS 20U

#endif

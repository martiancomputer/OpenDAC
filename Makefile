TARGET := build/opendac
CUBE := vendor/STM32CubeF4
HAL := $(CUBE)/Drivers/STM32F4xx_HAL_Driver
CMSIS := $(CUBE)/Drivers/CMSIS
DEVICE := $(CMSIS)/Device/ST/STM32F4xx
USB := $(CUBE)/Middlewares/ST/STM32_USB_Device_Library/Core

CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE := arm-none-eabi-size

CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFINES := -DSTM32F401xC -DUSE_HAL_DRIVER -DHSE_VALUE=25000000U
INCLUDES := -Iinclude -I$(DEVICE)/Include -I$(CMSIS)/Core/Include \
            -I$(HAL)/Inc -I$(HAL)/Inc/Legacy -I$(USB)/Inc
CFLAGS := $(CPU_FLAGS) $(DEFINES) $(INCLUDES) -std=c11 -O2 -g3 \
          -Wall -Wextra -Wshadow -Wconversion -Wno-unused-parameter \
          -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := $(CPU_FLAGS) -Tld/stm32f401cc.ld -nostartfiles \
           --specs=nosys.specs -Wl,--gc-sections -Wl,-Map=$(TARGET).map

APP_SOURCES := $(wildcard src/*.c)
HAL_SOURCES := $(addprefix $(HAL)/Src/, \
    stm32f4xx_hal.c stm32f4xx_hal_cortex.c stm32f4xx_hal_dma.c \
    stm32f4xx_hal_dma_ex.c stm32f4xx_hal_flash.c \
    stm32f4xx_hal_flash_ex.c stm32f4xx_hal_flash_ramfunc.c \
    stm32f4xx_hal_gpio.c stm32f4xx_hal_i2s.c \
    stm32f4xx_hal_i2s_ex.c stm32f4xx_hal_pcd.c \
    stm32f4xx_hal_pcd_ex.c stm32f4xx_hal_pwr.c \
    stm32f4xx_hal_pwr_ex.c stm32f4xx_hal_rcc.c \
    stm32f4xx_hal_rcc_ex.c stm32f4xx_ll_usb.c)
USB_SOURCES := $(addprefix $(USB)/Src/, \
    usbd_core.c usbd_ctlreq.c usbd_ioreq.c)
SYSTEM_SOURCE := $(DEVICE)/Source/Templates/system_stm32f4xx.c
STARTUP := $(DEVICE)/Source/Templates/gcc/startup_stm32f401xc.s
OBJECTS := $(patsubst %.c,build/%.o,$(APP_SOURCES) $(HAL_SOURCES) \
            $(USB_SOURCES) $(SYSTEM_SOURCE)) \
           $(patsubst %.s,build/%.o,$(STARTUP))

.PHONY: all clean size flash-dfu test
all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJECTS) ld/stm32f401cc.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

build/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPU_FLAGS) -x assembler-with-cpp -c $< -o $@

clean:
	$(RM) -r build

size: $(TARGET).elf
	$(SIZE) $<

flash-dfu: $(TARGET).bin
	dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D $<

test: $(TARGET).elf
	python3 tests/check_descriptors.py $(TARGET).elf
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iinclude \
		tests/test_rate.c src/rate_control.c -o build/test_rate
	build/test_rate

HOST_CC := cc

-include $(OBJECTS:.o=.d)

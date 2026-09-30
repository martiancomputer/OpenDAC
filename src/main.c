#include "stm32f4xx_hal.h"
#include "usb_app.h"
#include "audio_engine.h"
#include "dfu.h"

USBD_HandleTypeDef usb_device;
extern PCD_HandleTypeDef usb_pcd;

// The vendor startup calls __libc_init_array; this C-only firmware has no
// constructors, but newlib still expects these two startup hooks.
void _init(void) {}
void _fini(void) {}

static bool system_clock_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 25U;
    osc.PLL.PLLN = 336U;
    osc.PLL.PLLP = RCC_PLLP_DIV4;
    osc.PLL.PLLQ = 7U;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;
    RCC_ClkInitTypeDef bus = {0};
    bus.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    bus.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    bus.AHBCLKDivider = RCC_SYSCLK_DIV1;
    bus.APB1CLKDivider = RCC_HCLK_DIV2;
    bus.APB2CLKDivider = RCC_HCLK_DIV1;
    return HAL_RCC_ClockConfig(&bus, FLASH_LATENCY_2) == HAL_OK;
}

static void fatal_error(void)
{
    __disable_irq();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef led = {0};
    led.Pin = GPIO_PIN_13;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &led);
    for (;;) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        for (volatile uint32_t wait = 0; wait < 200000U; ++wait) {}
    }
}

int main(void)
{
    if (HAL_Init() != HAL_OK || !system_clock_init() ||
        !audio_hw_init()) fatal_error();
    stream_reset(96000U);
    if (USBD_Init(&usb_device, &usb_descriptors, 0U) != USBD_OK ||
        USBD_RegisterClass(&usb_device, &usb_audio_class) != USBD_OK ||
        USBD_Start(&usb_device) != USBD_OK) fatal_error();

    for (;;) {
        stream_service();
        if (dfu_pending()) {
            HAL_Delay(50U); // let DFU_DETACH status ZLP finish
            if (USBD_DeInit(&usb_device) != USBD_OK) fatal_error();
            HAL_Delay(100U); // let the host observe D+ disconnect
            dfu_enter_rom();
        }
    }
}

#include "stm32f4xx_hal.h"
#include "usb_app.h"
extern PCD_HandleTypeDef usb_pcd;
void SysTick_Handler(void) { HAL_IncTick(); }
void OTG_FS_IRQHandler(void) { HAL_PCD_IRQHandler(&usb_pcd); }
void HardFault_Handler(void) { for (;;) {} }

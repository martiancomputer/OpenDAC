#include "dfu.h"
#include "stm32f4xx_hal.h"

#define ROM_VECTOR_BASE 0x1FFF0000UL
static volatile bool detach_requested;

void dfu_request(void) { detach_requested = true; }
bool dfu_pending(void) { return detach_requested; }

void dfu_enter_rom(void)
{
    const uint32_t initial_sp = *(const volatile uint32_t *)ROM_VECTOR_BASE;
    const uint32_t initial_pc = *(const volatile uint32_t *)(ROM_VECTOR_BASE + 4U);
    __disable_irq();
    // The USB status stage and physical detach have completed in main().
    // Leave the factory ROM a reset-like peripheral and interrupt state.
    (void)HAL_RCC_DeInit();
    (void)HAL_DeInit();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (uint32_t i = 0; i < 8U; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
    SCB->VTOR = ROM_VECTOR_BASE;
    __DSB();
    __ISB();
    // Both operands are registers before changing MSP; there is no C call
    // or stack access between stack replacement and the ROM branch.
    __asm volatile ("msr msp, %0\n"
                    "cpsie i\n"
                    "bx %1\n"
                    : : "r"(initial_sp), "r"(initial_pc) : "memory");
    __builtin_unreachable();
}

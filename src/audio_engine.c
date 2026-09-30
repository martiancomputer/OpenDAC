#include "audio_engine.h"
#include "rate_control.h"
#include "diag.h"
#include "stm32f4xx_hal.h"
#include "hardware.h"

static I2S_HandleTypeDef i2s;
static DMA_HandleTypeDef dma_tx;
static volatile bool dma_fault;
static uint16_t active_n;
static uint16_t active_divisor;
static volatile bool dac_muted = true;
static volatile bool amp_muted = true;

typedef struct {
    uint32_t hz;
    uint16_t pll_n;
    uint8_t pll_r;
    uint16_t divisor; // 2 * I2SDIV + ODD
} audio_rate_profile_t;

static const audio_rate_profile_t rate_profiles[] = {
    {44100U, 271U, 2U, 48U}, // theoretical nominal; not hardware-validated
    {48000U, 384U, 5U, 25U}, // established prototype starting point
    {96000U, 424U, 3U, 23U}
};

static const audio_rate_profile_t *find_rate_profile(uint32_t hz)
{
    for (uint32_t i = 0; i < sizeof(rate_profiles) / sizeof(rate_profiles[0]);
         ++i)
        if (rate_profiles[i].hz == hz) return &rate_profiles[i];
    return NULL;
}

void audio_set_mute(bool mute)
{
    HAL_GPIO_WritePin(PCM_XSMT_PORT, PCM_XSMT_PIN,
                      mute ? GPIO_PIN_RESET : GPIO_PIN_SET);
    dac_muted = mute;
}

void audio_output_mute(void)
{
    // Headphone stage off first; never amplify a DAC transition.
#if CONFIG_TPA6138A2
    HAL_GPIO_WritePin(TPA_MUTE_PORT, TPA_MUTE_PIN, GPIO_PIN_RESET);
#endif
    amp_muted = true;
    audio_set_mute(true);
}

void audio_dac_unmute(void) { audio_set_mute(false); }
void audio_amp_unmute(void)
{
#if CONFIG_TPA6138A2
    HAL_GPIO_WritePin(TPA_MUTE_PORT, TPA_MUTE_PIN, GPIO_PIN_SET);
    amp_muted = false;
#endif
}
bool audio_dac_is_muted(void) { return dac_muted; }
bool audio_amp_is_muted(void) { return amp_muted; }
bool audio_amp_control_enabled(void) { return CONFIG_TPA6138A2 != 0; }

bool audio_hw_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    audio_output_mute();
    GPIO_InitTypeDef pins = {0};
    pins.Pin = PCM_XSMT_PIN;
    pins.Mode = GPIO_MODE_OUTPUT_PP;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pins);
#if CONFIG_TPA6138A2
    HAL_GPIO_WritePin(TPA_MUTE_PORT, TPA_MUTE_PIN, GPIO_PIN_RESET);
    pins.Pin = TPA_MUTE_PIN;
    HAL_GPIO_Init(TPA_MUTE_PORT, &pins);
#endif

    pins.Pin = GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_15;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &pins);

    i2s.Instance = SPI2;
    dma_tx.Instance = DMA1_Stream4;
    dma_tx.Init.Channel = DMA_CHANNEL_0;
    dma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    dma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    dma_tx.Init.MemInc = DMA_MINC_ENABLE;
    dma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    dma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    dma_tx.Init.Mode = DMA_CIRCULAR;
    dma_tx.Init.Priority = DMA_PRIORITY_HIGH;
    dma_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&dma_tx) != HAL_OK) return false;
    __HAL_LINKDMA(&i2s, hdmatx, dma_tx);
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
    return true;
}

bool audio_select_rate(uint32_t hz)
{
    const audio_rate_profile_t *profile = find_rate_profile(hz);
    if (profile == NULL) return false;

    audio_output_mute();
    rate_override_t saved = rate_override_for(hz);
    const uint16_t n = saved.valid ? saved.pll_n : profile->pll_n;
    const uint16_t divisor = saved.valid ? saved.divisor : profile->divisor;
    if (i2s.State != HAL_I2S_STATE_RESET &&
        HAL_I2S_DeInit(&i2s) != HAL_OK) return false;
#ifdef RCC_CFGR_I2SSRC
    CLEAR_BIT(RCC->CFGR, RCC_CFGR_I2SSRC);
#endif
    RCC_PeriphCLKInitTypeDef pll = {0};
    pll.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    pll.PLLI2S.PLLI2SN = n;
    pll.PLLI2S.PLLI2SR = profile->pll_r;
    if (HAL_RCCEx_PeriphCLKConfig(&pll) != HAL_OK ||
        __HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY) == RESET) return false;

    i2s.Init.Mode = I2S_MODE_MASTER_TX;
    i2s.Init.Standard = I2S_STANDARD_PHILIPS;
    i2s.Init.DataFormat = I2S_DATAFORMAT_24B;
    i2s.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
    i2s.Init.AudioFreq = hz;
    i2s.Init.CPOL = I2S_CPOL_LOW;
    i2s.Init.ClockSource = I2S_CLOCK_PLL;
    i2s.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&i2s) != HAL_OK) return false;
    // HAL computes from the configured HSE assumption. The measured board
    // ran at ~0.3907 of nominal, so use the explicit audited preset and the
    // bounded per-rate override after a USB-SOF measurement.
    SPI2->I2SPR = (divisor >> 1U) | ((divisor & 1U) << 8U);
    active_n = n;
    active_divisor = divisor;
    dma_fault = false;
    return true;
}

bool audio_start_dma(uint16_t *data, uint16_t halfwords)
{
    if ((halfwords & 1U) != 0U ||
        HAL_I2S_Transmit_DMA(&i2s, data, halfwords / 2U) != HAL_OK)
        return false;
    return __HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY) != RESET;
}

bool audio_stop_dma(void)
{
    audio_output_mute();
    if (i2s.State == HAL_I2S_STATE_RESET ||
        i2s.State == HAL_I2S_STATE_READY) return true;
    return HAL_I2S_DMAStop(&i2s) == HAL_OK;
}

uint32_t audio_dma_remaining(void) { return dma_tx.Instance->NDTR; }
uint16_t audio_clock_n(void) { return active_n; }
uint16_t audio_clock_divisor(void) { return active_divisor; }
void audio_request_fault(void) { dma_fault = true; ++diag_counts.dma_errors; }
bool audio_fault_pending(void) { return dma_fault; }
void audio_clear_fault(void) { dma_fault = false; }

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *handle)
{
    if (handle == &i2s) ++diag_counts.dma_half;
}
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *handle)
{
    if (handle == &i2s) ++diag_counts.dma_full;
}
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *handle)
{
    if (handle == &i2s) audio_request_fault();
}
void DMA1_Stream4_IRQHandler(void) { HAL_DMA_IRQHandler(&dma_tx); }

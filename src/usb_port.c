#include "usb_app.h"
#include "stm32f4xx_hal.h"

PCD_HandleTypeDef usb_pcd;

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd)
{
    (void)pcd;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &pins);
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd)
{
    (void)pcd;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *pcd)
{ (void)USBD_LL_SetupStage(pcd->pData, (uint8_t *)pcd->Setup); }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{ (void)USBD_LL_DataOutStage(pcd->pData, ep, pcd->OUT_ep[ep].xfer_buff); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{ (void)USBD_LL_DataInStage(pcd->pData, ep, pcd->IN_ep[ep].xfer_buff); }
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *pcd)
{ (void)USBD_LL_SOF(pcd->pData); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd)
{
    (void)USBD_LL_SetSpeed(pcd->pData, USBD_SPEED_FULL);
    (void)USBD_LL_Reset(pcd->pData);
}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *pcd)
{ (void)USBD_LL_Suspend(pcd->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *pcd)
{ (void)USBD_LL_Resume(pcd->pData); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *pcd)
{ (void)USBD_LL_DevConnected(pcd->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *pcd)
{ (void)USBD_LL_DevDisconnected(pcd->pData); }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{ (void)USBD_LL_IsoOUTIncomplete(pcd->pData, ep); }
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{ (void)USBD_LL_IsoINIncomplete(pcd->pData, ep); }

static USBD_StatusTypeDef usb_result(HAL_StatusTypeDef result)
{ return result == HAL_OK ? USBD_OK : USBD_FAIL; }

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    usb_pcd.Instance = USB_OTG_FS;
    usb_pcd.Init.dev_endpoints = 4U;
    usb_pcd.Init.speed = PCD_SPEED_FULL;
    usb_pcd.Init.dma_enable = DISABLE;
    usb_pcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    usb_pcd.Init.Sof_enable = ENABLE;
    usb_pcd.Init.low_power_enable = DISABLE;
    usb_pcd.Init.lpm_enable = DISABLE;
    usb_pcd.Init.vbus_sensing_enable = DISABLE;
    usb_pcd.Init.use_dedicated_ep1 = DISABLE;
    usb_pcd.pData = pdev;
    pdev->pData = &usb_pcd;
    if (HAL_PCD_Init(&usb_pcd) != HAL_OK ||
        HAL_PCDEx_SetRxFiFo(&usb_pcd, 0x120U) != HAL_OK ||
        HAL_PCDEx_SetTxFiFo(&usb_pcd, 0U, 0x10U) != HAL_OK ||
        HAL_PCDEx_SetTxFiFo(&usb_pcd, 1U, 0x10U) != HAL_OK)
        return USBD_FAIL;
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *p)
{ return usb_result(HAL_PCD_DeInit(p->pData)); }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *p)
{ return usb_result(HAL_PCD_Start(p->pData)); }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *p)
{ return usb_result(HAL_PCD_Stop(p->pData)); }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *p, uint8_t ep,
                                  uint8_t type, uint16_t mps)
{ return usb_result(HAL_PCD_EP_Open(p->pData, ep, mps, type)); }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *p, uint8_t ep)
{ return usb_result(HAL_PCD_EP_Close(p->pData, ep)); }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *p, uint8_t ep)
{ return usb_result(HAL_PCD_EP_Flush(p->pData, ep)); }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *p, uint8_t ep)
{ return usb_result(HAL_PCD_EP_SetStall(p->pData, ep)); }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *p, uint8_t ep)
{ return usb_result(HAL_PCD_EP_ClrStall(p->pData, ep)); }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *p, uint8_t ep)
{
    PCD_HandleTypeDef *pcd = p->pData;
    return (ep & 0x80U) ? pcd->IN_ep[ep & 0x7FU].is_stall :
                          pcd->OUT_ep[ep & 0x7FU].is_stall;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *p, uint8_t addr)
{ return usb_result(HAL_PCD_SetAddress(p->pData, addr)); }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *p, uint8_t ep,
                                    uint8_t *data, uint32_t len)
{ return usb_result(HAL_PCD_EP_Transmit(p->pData, ep, data, len)); }
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *p, uint8_t ep,
                                          uint8_t *data, uint32_t len)
{ return usb_result(HAL_PCD_EP_Receive(p->pData, ep, data, len)); }
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *p, uint8_t ep)
{ return HAL_PCD_EP_GetRxCount(p->pData, ep); }
void USBD_LL_Delay(uint32_t ms) { HAL_Delay(ms); }

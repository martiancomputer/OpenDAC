#include "usb_app.h"
#include "audio_engine.h"
#include "diag.h"
#include "dfu.h"
#include "project.h"
#include <string.h>

enum {
    CONFIG_LEN = 142,
    FEATURE_UNIT = 2,
    CMD_SET_CUR = 0x01,
    CMD_GET_CUR = 0x81,
    CMD_GET_MIN = 0x82,
    CMD_GET_MAX = 0x83,
    CMD_GET_RES = 0x84
};

// UAC1 AudioControl + AudioStreaming alt 0/1 + asynchronous OUT feedback
// endpoint + DFU runtime. 24-bit packed USB data at 44.1/48/96 kHz.
static uint8_t config_descriptor[] = {
    9, 2, CONFIG_LEN, 0, 3, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 1, 1, 0, 0,
    9, 0x24, 1, 0, 1, 39, 0, 1, 1,
    12, 0x24, 2, 1, 1, 1, 0, 2, 3, 0, 0, 0,
    9, 0x24, 6, FEATURE_UNIT, 1, 1, 3, 0, 0,
    9, 0x24, 3, 3, 1, 3, 0, FEATURE_UNIT, 0,
    9, 4, AUDIO_INTERFACE, 0, 0, 1, 2, 0, 0,
    9, 4, AUDIO_INTERFACE, 1, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,
    17, 0x24, 2, 1, 2, 3, 24, 3,
        0x44, 0xAC, 0x00, // 44100, requires physical validation
        0x80, 0xBB, 0x00, // 48000
        0x00, 0x77, 0x01, // 96000
    9, 5, USB_AUDIO_OUT, 0x05,
        USB_PACKET_BYTES & 0xFF, USB_PACKET_BYTES >> 8, 1, 0, USB_FEEDBACK_IN,
    7, 0x25, 1, 1, 0, 0, 0,
    9, 5, USB_FEEDBACK_IN, 0x11, 3, 0, 1, 2, 0,
    9, 4, DFU_INTERFACE, 0, 0, 0xFE, 1, 1, 0,
    9, 0x21, 0x0B, 0xFF, 0, 0, 4, 0x1A, 1
};
_Static_assert(sizeof(config_descriptor) == CONFIG_LEN, "USB descriptor length");

typedef enum { CTRL_NONE, CTRL_FREQUENCY, CTRL_VOLUME, CTRL_MUTE } control_t;
typedef struct {
    uint8_t alt;
    uint8_t feedback_busy;
    uint8_t feedback_interval;
    uint8_t muted;
    int16_t volume_db256;
    uint8_t control_buffer[4];
    control_t pending_control;
    uint8_t feedback[4];
    uint8_t answer[8];
    uint32_t rx_words[(USB_PACKET_BYTES + 3U) / 4U];
} usb_audio_state_t;
static usb_audio_state_t audio_usb;

static uint8_t *configuration_descriptor(uint16_t *len)
{ *len = sizeof(config_descriptor); return config_descriptor; }

static uint8_t fail(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    USBD_CtlError(pdev, req);
    return USBD_FAIL;
}

static uint8_t class_init(USBD_HandleTypeDef *pdev, uint8_t configuration)
{
    (void)configuration;
    memset(&audio_usb, 0, sizeof(audio_usb));
    pdev->pClassData = &audio_usb;
    if (USBD_LL_OpenEP(pdev, USB_AUDIO_OUT, USBD_EP_TYPE_ISOC,
                       USB_PACKET_BYTES) != USBD_OK ||
        USBD_LL_OpenEP(pdev, USB_FEEDBACK_IN, USBD_EP_TYPE_ISOC,
                       3U) != USBD_OK) return USBD_FAIL;
    pdev->ep_out[USB_AUDIO_OUT].is_used = 1U;
    pdev->ep_in[USB_FEEDBACK_IN & 0xFU].is_used = 1U;
    // OUT is armed only when the host selects streaming alt 1. Arming at
    // configuration time caused an ISO incomplete interrupt every idle SOF.
    return USBD_OK;
}

static uint8_t class_deinit(USBD_HandleTypeDef *pdev, uint8_t configuration)
{
    (void)configuration;
    stream_set_active(false);
    pdev->pClassData = NULL;
    pdev->ep_out[USB_AUDIO_OUT].is_used = 0U;
    pdev->ep_in[USB_FEEDBACK_IN & 0xFU].is_used = 0U;
    const USBD_StatusTypeDef a = USBD_LL_CloseEP(pdev, USB_AUDIO_OUT);
    const USBD_StatusTypeDef b = USBD_LL_CloseEP(pdev, USB_FEEDBACK_IN);
    return a == USBD_OK && b == USBD_OK ? USBD_OK : USBD_FAIL;
}

static uint8_t send_control(USBD_HandleTypeDef *pdev, uint16_t value,
                            uint16_t bytes)
{
    audio_usb.answer[0] = (uint8_t)(value & 0xFFU);
    audio_usb.answer[1] = (uint8_t)(value >> 8U);
    return USBD_CtlSendData(pdev, audio_usb.answer, bytes);
}

static uint8_t class_setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    const uint8_t recipient = req->bmRequest & 0x1FU;
    const uint8_t type = req->bmRequest & USB_REQ_TYPE_MASK;
    const uint8_t iface = req->wIndex & 0xFFU;
    if (type == USB_REQ_TYPE_VENDOR && recipient == USB_REQ_RECIPIENT_DEVICE &&
        (req->bmRequest & 0x80U) && req->wLength <= 64U) {
        uint16_t len = 0;
        uint8_t *payload = req->bRequest == 0x5AU ?
                           diag_status(&len) :
                           req->bRequest == 0x5BU ?
                           diag_page(req->wIndex, &len) :
                           req->bRequest == 0x5CU ?
                           diag_extended(req->wIndex, &len) : NULL;
        if (payload != NULL) {
            if (len > req->wLength) len = req->wLength;
            return USBD_CtlSendData(pdev, payload, len);
        }
    }
    if (type == USB_REQ_TYPE_CLASS && recipient == USB_REQ_RECIPIENT_INTERFACE &&
        iface == DFU_INTERFACE) {
        if (req->bRequest == 0U && req->wLength == 0U) {
            dfu_request();
            return USBD_OK;
        }
        if (req->bRequest == 3U && req->wLength >= 6U) {
            memset(audio_usb.answer, 0, 6U);
            audio_usb.answer[4] = dfu_pending() ? 1U : 0U;
            return USBD_CtlSendData(pdev, audio_usb.answer, 6U);
        }
        if (req->bRequest == 5U && req->wLength >= 1U) {
            audio_usb.answer[0] = dfu_pending() ? 1U : 0U;
            return USBD_CtlSendData(pdev, audio_usb.answer, 1U);
        }
        return fail(pdev, req);
    }
    if (type == USB_REQ_TYPE_STANDARD &&
        recipient == USB_REQ_RECIPIENT_INTERFACE) {
        if (req->bRequest == USB_REQ_GET_INTERFACE && req->wLength >= 1U) {
            audio_usb.answer[0] = (uint8_t)(iface == AUDIO_INTERFACE ?
                                             audio_usb.alt : 0U);
            return USBD_CtlSendData(pdev, audio_usb.answer, 1U);
        }
        if (req->bRequest == USB_REQ_SET_INTERFACE && req->wLength == 0U) {
            if (iface == AUDIO_INTERFACE && req->wValue <= 1U) {
                audio_usb.alt = (uint8_t)req->wValue;
                stream_set_active(audio_usb.alt != 0U);
                if (audio_usb.alt == 1U)
                    return USBD_LL_PrepareReceive(
                        pdev, USB_AUDIO_OUT, (uint8_t *)audio_usb.rx_words,
                        USB_PACKET_BYTES);
                return USBD_OK;
            }
            return iface == 0U || iface == DFU_INTERFACE ?
                   (req->wValue == 0U ? USBD_OK : fail(pdev, req)) :
                   fail(pdev, req);
        }
        if (req->bRequest == USB_REQ_GET_STATUS && req->wLength >= 2U)
            return send_control(pdev, 0U, 2U);
    }
    if (type == USB_REQ_TYPE_CLASS && recipient == USB_REQ_RECIPIENT_ENDPOINT &&
        (req->wIndex & 0xFFU) == USB_AUDIO_OUT &&
        (req->wValue >> 8U) == 1U) {
        if (req->bRequest == CMD_GET_CUR && req->wLength >= 3U) {
            uint32_t hz = stream_rate();
            audio_usb.answer[0] = (uint8_t)hz;
            audio_usb.answer[1] = (uint8_t)(hz >> 8U);
            audio_usb.answer[2] = (uint8_t)(hz >> 16U);
            return USBD_CtlSendData(pdev, audio_usb.answer, 3U);
        }
        if (req->bRequest == CMD_SET_CUR && req->wLength == 3U) {
            audio_usb.pending_control = CTRL_FREQUENCY;
            return USBD_CtlPrepareRx(pdev, audio_usb.control_buffer, 3U);
        }
    }
    if (type == USB_REQ_TYPE_CLASS && recipient == USB_REQ_RECIPIENT_INTERFACE &&
        iface == 0U && (req->wIndex >> 8U) == FEATURE_UNIT &&
        (req->wValue & 0xFFU) == 0U) {
        const uint8_t selector = (uint8_t)(req->wValue >> 8U);
        if (req->bRequest == CMD_SET_CUR) {
            if (selector == 1U && req->wLength == 1U)
                audio_usb.pending_control = CTRL_MUTE;
            else if (selector == 2U && req->wLength == 2U)
                audio_usb.pending_control = CTRL_VOLUME;
            else return fail(pdev, req);
            return USBD_CtlPrepareRx(pdev, audio_usb.control_buffer,
                                     req->wLength);
        }
        if (selector == 1U && req->bRequest == CMD_GET_CUR &&
            req->wLength >= 1U)
            return send_control(pdev, audio_usb.muted, 1U);
        if (selector == 2U && req->wLength >= 2U) {
            if (req->bRequest == CMD_GET_CUR)
                return send_control(pdev,
                                    (uint16_t)audio_usb.volume_db256, 2U);
            if (req->bRequest == CMD_GET_MIN)
                return send_control(pdev, 0xA000U, 2U);
            if (req->bRequest == CMD_GET_MAX)
                return send_control(pdev, 0U, 2U);
            if (req->bRequest == CMD_GET_RES)
                return send_control(pdev, 0x0300U, 2U);
        }
    }
    return fail(pdev, req);
}

static uint8_t class_control_ready(USBD_HandleTypeDef *pdev)
{
    (void)pdev;
    const control_t command = audio_usb.pending_control;
    audio_usb.pending_control = CTRL_NONE;
    if (command == CTRL_FREQUENCY) {
        uint32_t hz = audio_usb.control_buffer[0] |
                      ((uint32_t)audio_usb.control_buffer[1] << 8U) |
                      ((uint32_t)audio_usb.control_buffer[2] << 16U);
        if (hz == 44100U || hz == 48000U || hz == 96000U)
            stream_request_rate(hz);
        else return USBD_FAIL;
    } else if (command == CTRL_MUTE) {
        audio_usb.muted = audio_usb.control_buffer[0] != 0U;
    } else if (command == CTRL_VOLUME) {
        int16_t value = (int16_t)(audio_usb.control_buffer[0] |
                                  ((uint16_t)audio_usb.control_buffer[1] << 8U));
        if (value >= -24576 && value <= 0) audio_usb.volume_db256 = value;
    }
    stream_set_controls(audio_usb.muted != 0U, audio_usb.volume_db256);
    return USBD_OK;
}

static uint8_t class_in(USBD_HandleTypeDef *pdev, uint8_t ep)
{
    (void)pdev;
    if (ep == (USB_FEEDBACK_IN & 0x7FU)) audio_usb.feedback_busy = 0U;
    return USBD_OK;
}

static uint8_t rearm_out(USBD_HandleTypeDef *pdev)
{
    return USBD_LL_PrepareReceive(pdev, USB_AUDIO_OUT,
                                  (uint8_t *)audio_usb.rx_words,
                                  USB_PACKET_BYTES);
}

static uint8_t class_out(USBD_HandleTypeDef *pdev, uint8_t ep)
{
    if (ep != USB_AUDIO_OUT) return USBD_FAIL;
    const uint32_t bytes = USBD_GetRxCount(pdev, USB_AUDIO_OUT);
    if (audio_usb.alt == 1U) {
        if (bytes > UINT16_MAX) return USBD_FAIL;
        (void)stream_receive((const uint8_t *)audio_usb.rx_words,
                             (uint16_t)bytes);
    }
    return rearm_out(pdev);
}

static uint8_t class_sof(USBD_HandleTypeDef *pdev)
{
    stream_sof();
    if (audio_usb.alt != 1U || ++audio_usb.feedback_interval < 4U ||
        audio_usb.feedback_busy) return USBD_OK;
    audio_usb.feedback_interval = 0U;
    uint32_t value = stream_feedback_q14();
    audio_usb.feedback[0] = (uint8_t)value;
    audio_usb.feedback[1] = (uint8_t)(value >> 8U);
    audio_usb.feedback[2] = (uint8_t)(value >> 16U);
    if (USBD_LL_Transmit(pdev, USB_FEEDBACK_IN,
                         audio_usb.feedback, 3U) != USBD_OK) return USBD_FAIL;
    audio_usb.feedback_busy = 1U;
    return USBD_OK;
}

static uint8_t class_incomplete_in(USBD_HandleTypeDef *pdev, uint8_t ep)
{ (void)pdev; (void)ep; audio_usb.feedback_busy = 0U; return USBD_OK; }
static uint8_t class_incomplete_out(USBD_HandleTypeDef *pdev, uint8_t ep)
{
    if (ep != USB_AUDIO_OUT) return USBD_FAIL;
    if (audio_usb.alt != 1U) return USBD_OK;
    ++diag_counts.usb_incomplete;
    return rearm_out(pdev); // never arm into the DMA ring
}

USBD_ClassTypeDef usb_audio_class = {
    .Init = class_init,
    .DeInit = class_deinit,
    .Setup = class_setup,
    .EP0_RxReady = class_control_ready,
    .DataIn = class_in,
    .DataOut = class_out,
    .SOF = class_sof,
    .IsoINIncomplete = class_incomplete_in,
    .IsoOUTIncomplete = class_incomplete_out,
    .GetHSConfigDescriptor = configuration_descriptor,
    .GetFSConfigDescriptor = configuration_descriptor,
    .GetOtherSpeedConfigDescriptor = configuration_descriptor,
};

#include "usb_app.h"
#include "usbd_ctlreq.h"

static uint8_t device_descriptor[] = {
    18, USB_DESC_TYPE_DEVICE, 0x00, 0x02,
    0, 0, 0, USB_MAX_EP0_SIZE,
    0x83, 0x04, 0x30, 0x57, // prototype VID/PID; see README
    0x00, 0x02, 1, 2, 0, 1
};
static uint8_t language_descriptor[] = {
    4, USB_DESC_TYPE_STRING, 0x09, 0x04
};
static uint8_t text_descriptor[USBD_MAX_STR_DESC_SIZ];

static uint8_t *device(USBD_SpeedTypeDef speed, uint16_t *len)
{ (void)speed; *len = sizeof(device_descriptor); return device_descriptor; }
static uint8_t *language(USBD_SpeedTypeDef speed, uint16_t *len)
{ (void)speed; *len = sizeof(language_descriptor); return language_descriptor; }
static uint8_t *manufacturer(USBD_SpeedTypeDef speed, uint16_t *len)
{
    (void)speed;
    USBD_GetString((uint8_t *)"OpenDAC", text_descriptor, len);
    return text_descriptor;
}
static uint8_t *product(USBD_SpeedTypeDef speed, uint16_t *len)
{
    (void)speed;
    USBD_GetString((uint8_t *)"PCM5102A DAC", text_descriptor, len);
    return text_descriptor;
}
static uint8_t *serial(USBD_SpeedTypeDef speed, uint16_t *len)
{ (void)speed; *len = 0U; return NULL; }
static uint8_t *configuration(USBD_SpeedTypeDef speed, uint16_t *len)
{
    (void)speed;
    USBD_GetString((uint8_t *)"Audio and DFU", text_descriptor, len);
    return text_descriptor;
}
static uint8_t *interface(USBD_SpeedTypeDef speed, uint16_t *len)
{
    (void)speed;
    USBD_GetString((uint8_t *)"USB Audio", text_descriptor, len);
    return text_descriptor;
}

USBD_DescriptorsTypeDef usb_descriptors = {
    device, language, manufacturer, product, serial, configuration, interface
};

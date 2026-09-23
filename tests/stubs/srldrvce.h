/* Stand-in for CEdev's srldrvce.h, wired to the fake board in stubs.c. */
#ifndef SRLDRVCE_H
#define SRLDRVCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <usbdrvce.h>

typedef uint32_t uint24_t;

typedef enum { SRL_SUCCESS = 0, SRL_ERROR_INVALID_DEVICE = -4 } srl_error_t;
typedef struct {
    usb_device_t dev;
} srl_device_t;

#define SRL_INTERFACE_ANY 0xFF

srl_error_t srl_Open(srl_device_t *srl, usb_device_t dev, void *buffer, size_t size,
                     uint8_t interface, uint24_t rate);
void srl_Close(srl_device_t *srl);
int srl_Read(srl_device_t *srl, void *data, size_t length);
int srl_Write(srl_device_t *srl, const void *data, size_t length);
const usb_standard_descriptors_t *srl_GetCDCStandardDescriptors(void);
usb_error_t srl_UsbEventCallback(usb_event_t event, void *event_data,
                                 usb_callback_data_t *callback_data);

#endif

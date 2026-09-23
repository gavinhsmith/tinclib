/* Stand-in for CEdev's usbdrvce.h: just what tinclib uses. See stubs.c. */
#ifndef USBDRVCE_H
#define USBDRVCE_H

#include <stddef.h>
#include <stdint.h>

typedef enum { USB_SUCCESS = 0, USB_ERROR_FAILED = 1 } usb_error_t;
typedef enum {
    USB_DEVICE_DISCONNECTED_EVENT,
    USB_DEVICE_CONNECTED_EVENT,
    USB_DEVICE_ENABLED_EVENT,
    USB_HOST_CONFIGURE_EVENT
} usb_event_t;
typedef struct usb_device *usb_device_t;
typedef struct usb_standard_descriptors usb_standard_descriptors_t;
#define usb_callback_data_t void
typedef usb_error_t (*usb_event_callback_t)(usb_event_t, void *, usb_callback_data_t *);
typedef uint8_t usb_role_t;

#define USB_DEFAULT_INIT_FLAGS 0
#define USB_SKIP_HUBS (1 << 3)
#define USB_ROLE_DEVICE (1 << 4)

usb_error_t usb_Init(usb_event_callback_t handler, usb_callback_data_t *data,
                     const usb_standard_descriptors_t *desc, unsigned flags);
void usb_Cleanup(void);
usb_error_t usb_HandleEvents(void);
usb_device_t usb_FindDevice(usb_device_t root, usb_device_t from, unsigned flags);
usb_error_t usb_ResetDevice(usb_device_t device);
usb_role_t usb_GetRole(void);

#endif

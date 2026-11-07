// SPDX-License-Identifier: Apache-2.0
// Host-only TinyUSB/Pico seam: production protocol and transport run unmodified.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_DEBUG 0
#define CONTROL_STAGE_SETUP 0
#define TUSB_CLASS_VENDOR_SPECIFIC 255
#define TUSB_DESC_ENDPOINT 5
#define TUSB_XFER_BULK 2
#define TUSB_DIR_IN 1
#define USB_OUTPUT_MODE_SWITCH2_GC 17
typedef enum { XFER_RESULT_SUCCESS, XFER_RESULT_FAILED } xfer_result_t;
#pragma pack(push, 1)
typedef struct { uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting,
    bNumEndpoints, bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
} tusb_desc_interface_t;
typedef struct { uint8_t bLength, bDescriptorType, bEndpointAddress;
    struct { unsigned char xfer:2; unsigned char rest:6; } bmAttributes;
    uint16_t wMaxPacketSize; uint8_t bInterval;
} tusb_desc_endpoint_t;
typedef struct { uint8_t bmRequestType, bRequest; uint16_t wValue, wIndex, wLength;
} tusb_control_request_t;
#pragma pack(pop)
typedef struct {
    void (*init)(void); void (*reset)(uint8_t);
    uint16_t (*open)(uint8_t, const tusb_desc_interface_t *, uint16_t);
    bool (*control_xfer_cb)(uint8_t, uint8_t, const tusb_control_request_t *);
    bool (*xfer_cb)(uint8_t, uint8_t, xfer_result_t, uint32_t);
} usbd_class_driver_t;
typedef struct { uint32_t unused; } input_event_t;
typedef struct { uint8_t left_x, left_y, right_x, right_y, l2_analog, r2_analog; } profile_output_t;
typedef struct { uint8_t rumble_left, rumble_right, led_player, led_r, led_g, led_b; bool dirty; } output_feedback_t;
typedef struct {
    const char *name; int mode;
    const uint8_t *(*get_device_descriptor)(void), *(*get_config_descriptor)(void), *(*get_report_descriptor)(void);
    void (*init)(void);
    bool (*send_report)(uint8_t, const input_event_t *, const profile_output_t *, uint32_t);
    void (*handle_output)(uint8_t, const uint8_t *, uint16_t);
    uint8_t (*get_rumble)(void); bool (*get_feedback)(output_feedback_t *);
    const usbd_class_driver_t *(*get_class_driver)(void); void (*task)(void);
} usbd_mode_t;
static inline uint8_t tu_edpt_dir(uint8_t ep) { return ep >> 7; }
bool tud_ready(void);
bool tud_hid_n_ready(uint8_t instance);
bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, uint16_t len);
bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *req, void *data, uint16_t len);
bool tud_control_status(uint8_t rhport, const tusb_control_request_t *req);
bool usbd_edpt_open(uint8_t rhport, const tusb_desc_endpoint_t *ep);
bool usbd_edpt_busy(uint8_t rhport, uint8_t ep);
bool usbd_edpt_claim(uint8_t rhport, uint8_t ep);
bool usbd_edpt_xfer(uint8_t rhport, uint8_t ep, uint8_t *data, uint16_t len);
uint32_t get_absolute_time(void);
static inline uint32_t to_ms_since_boot(uint32_t t) { return t; }

// SPDX-License-Identifier: Apache-2.0
// Native NSO GameCube USB transport and Joypad input/feedback seam.
#include "../usbd_mode.h"
#include "../tusb_compat.h"
#include "core/buttons.h"
#include "switch2_gc/switch_gc.h"
#include "switch2_gc/joypad_gc_transport.h"
#include <string.h>

static nso_gc_input_t input;
static output_feedback_t feedback;

// Own only IF1. TinyUSB's built-in HID driver owns IF0. One command at a
// time: OUT is rearmed only after its reply completes. This preserves USB
// packet boundaries, applies backpressure, and retains replies while IN is busy.
static struct {
    uint8_t rhport, ep_in, ep_out;
    uint16_t reply_len;
    bool out_armed, in_flight;
    CFG_TUSB_MEM_ALIGN uint8_t rx[64];
    CFG_TUSB_MEM_ALIGN uint8_t tx[64];
} bulk;

void joypad_gc_get_input(uint8_t player, nso_gc_input_t *out)
{
    (void)player;
    *out = input;
}

void joypad_gc_set_rumble(uint8_t player, uint8_t left, uint8_t right)
{
    (void)player;
    if (feedback.rumble_left != left || feedback.rumble_right != right) {
        feedback.rumble_left = left;
        feedback.rumble_right = right;
        feedback.dirty = true;
    }
}

void joypad_gc_set_player_leds(uint8_t player, uint8_t mask)
{
    (void)player;
    uint8_t index = 0;
    while (mask && !(mask & 1)) { mask >>= 1; index++; }
    feedback.led_player = mask ? index + 1 : 0;
    feedback.dirty = true;
}

void joypad_gc_queue_reply(const uint8_t *data, uint16_t len)
{
    // The receive endpoint cannot deliver a second command until this drains.
    if (!bulk.reply_len && !bulk.in_flight && len <= sizeof(bulk.tx)) {
        memcpy(bulk.tx, data, len);
        bulk.reply_len = len;
    }
}

void joypad_gc_bulk_task(void)
{
    if (!tud_ready() || !bulk.ep_in || bulk.in_flight) return;
    if (bulk.reply_len) {
        if (usbd_edpt_busy(bulk.rhport, bulk.ep_in) ||
            !usbd_edpt_claim(bulk.rhport, bulk.ep_in)) return;
        if (usbd_edpt_xfer(bulk.rhport, bulk.ep_in, bulk.tx, bulk.reply_len)) {
            bulk.in_flight = true;
        }
        // Failed queueing retains the response and retries on the next tick.
    } else if (!bulk.out_armed) {
        bulk.out_armed = usbd_edpt_xfer(bulk.rhport, bulk.ep_out, bulk.rx, sizeof(bulk.rx));
    }
}

static void driver_reset(uint8_t rhport)
{
    memset(&bulk, 0, sizeof(bulk));
    bulk.rhport = rhport;
    switch_gc_reset();
    // Always forward a stop after a session boundary, even if already silent.
    feedback.dirty = true;
}

static void driver_init(void) { driver_reset(0); }

static uint16_t driver_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    if (itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        itf->bInterfaceNumber != 1 || itf->bNumEndpoints != 2 || max_len < 23) return 0;
    uint8_t const *p = (uint8_t const *)itf + sizeof(*itf);
    for (unsigned i = 0; i < 2; i++, p += 7) {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
        if (ep->bLength != 7 || ep->bDescriptorType != TUSB_DESC_ENDPOINT ||
            ep->bmAttributes.xfer != TUSB_XFER_BULK || !usbd_edpt_open(rhport, ep)) return 0;
        if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) bulk.ep_in = ep->bEndpointAddress;
        else bulk.ep_out = ep->bEndpointAddress;
    }
    if (bulk.ep_in != 0x82 || bulk.ep_out != 0x02) return 0;
    bulk.rhport = rhport;
    switch_gc_mount();
    joypad_gc_bulk_task();
    return 23;
}

static bool driver_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport; (void)stage; (void)request;
    return false; // Vendor requests are routed by tud_vendor_control_xfer_cb.
}

static bool driver_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result, uint32_t len)
{
    (void)rhport;
    if (ep == bulk.ep_out) {
        bulk.out_armed = false;
        if (result == XFER_RESULT_SUCCESS && len <= sizeof(bulk.rx))
            switch_gc_vendor_dispatch(bulk.rx, len);
    } else if (ep == bulk.ep_in) {
        bulk.in_flight = false;
        // Failed transfers retry the identical reply; success frees the slot.
        if (result == XFER_RESULT_SUCCESS && len == bulk.reply_len) bulk.reply_len = 0;
    }
    joypad_gc_bulk_task();
    return true;
}

static const usbd_class_driver_t driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "NSO_GC_BULK",
#endif
    .init = driver_init, .reset = driver_reset, .open = driver_open,
    .control_xfer_cb = driver_control, .xfer_cb = driver_xfer,
};
static const usbd_class_driver_t *get_driver(void) { return &driver; }

static uint16_t axis12(uint8_t value)
{
    // Exact endpoints and center: 0 -> 0, 128 -> 2048, 255 -> 4095.
    return value <= 128 ? (uint16_t)value * 16 :
        2048 + ((uint16_t)(value - 128) * 2047 + 63) / 127;
}

static uint16_t axis12_y(uint8_t value)
{
    // Nintendo Y increases upwards; preserve the exact neutral point as well.
    return value <= 128 ? 2048 + ((uint16_t)(128 - value) * 2047 + 64) / 128 :
        2048 - ((uint16_t)(value - 128) * 2048 + 63) / 127;
}

static void mode_init(void)
{
    memset(&input, 0, sizeof(input));
    nso_gc_pack_stick(2048, 2048, input.left_stick);
    nso_gc_pack_stick(2048, 2048, input.right_stick);
    memset(&feedback, 0, sizeof(feedback));
    driver_init();
}

static bool send_report(uint8_t player, const input_event_t *event,
                        const profile_output_t *out, uint32_t buttons)
{
    (void)event;
    if (player != 0) return false;
    memset(input.buttons, 0, sizeof(input.buttons));
    input.extra = input.gc_extra = 0;
    if (buttons & JP_BUTTON_B1) input.buttons[0] |= NSOGC_MASK_A;
    if (buttons & JP_BUTTON_B2) input.buttons[0] |= NSOGC_MASK_B;
    if (buttons & JP_BUTTON_B3) input.buttons[0] |= NSOGC_MASK_X;
    if (buttons & JP_BUTTON_B4) input.buttons[0] |= NSOGC_MASK_Y;
    if (buttons & JP_BUTTON_S1) input.gc_extra |= NSOGC_NATIVE_Z;
    if (buttons & JP_BUTTON_L1) input.gc_extra |= NSOGC_NATIVE_L_DETENT;
    if (buttons & JP_BUTTON_R1) input.gc_extra |= NSOGC_NATIVE_R_DETENT;
    if (buttons & JP_BUTTON_S2) input.buttons[1] |= NSOGC_MASK_PLUS;
    if (buttons & JP_BUTTON_A1) input.buttons[1] |= NSOGC_MASK_HOME;
    if (buttons & JP_BUTTON_A2) input.buttons[1] |= NSOGC_MASK_CAPTURE;
    if (buttons & JP_BUTTON_DU) input.buttons[2] |= NSOGC_MASK_DPAD_UP;
    if (buttons & JP_BUTTON_DD) input.buttons[2] |= NSOGC_MASK_DPAD_DOWN;
    if (buttons & JP_BUTTON_DL) input.buttons[2] |= NSOGC_MASK_DPAD_LEFT;
    if (buttons & JP_BUTTON_DR) input.buttons[2] |= NSOGC_MASK_DPAD_RIGHT;
    // Native-mode-only menu shortcuts for original GC controllers.
    if ((buttons & (JP_BUTTON_S1 | JP_BUTTON_S2)) == (JP_BUTTON_S1 | JP_BUTTON_S2)) {
        input.gc_extra &= ~NSOGC_NATIVE_Z;
        input.buttons[1] &= ~NSOGC_MASK_PLUS;
        input.gc_extra |= NSOGC_NATIVE_ZL;
    } else if ((buttons & (JP_BUTTON_S2 | JP_BUTTON_DU)) == (JP_BUTTON_S2 | JP_BUTTON_DU)) {
        input.buttons[1] &= ~NSOGC_MASK_PLUS;
        input.buttons[2] &= ~NSOGC_MASK_DPAD_UP;
        input.buttons[1] |= NSOGC_MASK_HOME;
    } else if ((buttons & (JP_BUTTON_S2 | JP_BUTTON_DD)) == (JP_BUTTON_S2 | JP_BUTTON_DD)) {
        input.buttons[1] &= ~NSOGC_MASK_PLUS;
        input.buttons[2] &= ~NSOGC_MASK_DPAD_DOWN;
        input.buttons[1] |= NSOGC_MASK_MINUS;
    }
    nso_gc_pack_stick(axis12(out->left_x), axis12_y(out->left_y), input.left_stick);
    nso_gc_pack_stick(axis12(out->right_x), axis12_y(out->right_y), input.right_stick);
    // Continuous travel and final clicks stay independent. No digital fallback.
    input.left_trigger = out->l2_analog;
    input.right_trigger = out->r2_analog;
    return true; // Retained state is streamed by switch_gc_task(), even without changes.
}

static uint8_t get_rumble(void) { return feedback.rumble_left; }
static bool get_feedback(output_feedback_t *out)
{
    if (!out || !feedback.dirty) return false;
    *out = feedback;
    feedback.dirty = false;
    return true;
}

static void handle_output(uint8_t id, const uint8_t *data, uint16_t len)
{
    // TinyUSB supplies interrupt OUT with ID inline, SET_REPORT with ID separate.
    if (id == 0) {
        if (!len) return;
        id = *data++; len--;
    }
    switch_gc_hid_out_report(id, data, len);
}

const uint16_t *joypad_gc_string_descriptor(uint8_t index)
{
    static uint16_t buffer[64];
    if (index == 0xEE) return switch_gc_ms_os_string_descriptor();
    if (index == 0) { buffer[0] = 0x0304; buffer[1] = 0x0409; return buffer; }
    size_t count;
    const char **strings = switch_gc_string_table(&count);
    if (index >= count || !strings[index]) return NULL;
    size_t len = strlen(strings[index]);
    if (len > 63) len = 63;
    buffer[0] = (uint16_t)(0x0300 | (2 + 2 * len));
    for (size_t i = 0; i < len; i++) buffer[i + 1] = (uint8_t)strings[index][i];
    return buffer;
}

const usbd_mode_t switch2_gc_mode = {
    .name = "Switch 2 NSO GC", .mode = USB_OUTPUT_MODE_SWITCH2_GC,
    .get_device_descriptor = switch_gc_device_descriptor,
    .get_config_descriptor = switch_gc_config_descriptor,
    .get_report_descriptor = switch_gc_hid_report_descriptor,
    .init = mode_init, .send_report = send_report,
    .handle_output = handle_output, .get_rumble = get_rumble,
    .get_feedback = get_feedback, .get_class_driver = get_driver,
    .task = switch_gc_task,
};

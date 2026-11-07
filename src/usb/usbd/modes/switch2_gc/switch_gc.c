// SPDX-License-Identifier: Apache-2.0
// Adapted from PicoSwitch2 e054bfd1311289f392069ff0d2447e01748c2124.
// See README.md and LICENSE.PicoSwitch2 in this directory.

#include <stdio.h>
#include <string.h>

#include "pico/time.h"
#include "tusb.h"

#include "ns2_gc_identity.h"
#include "joypad_gc_transport.h"
#include "switch_gc_rumble_decode.h"
#include "switch_gc.h"
#include "switch_gc_encode.h"
#include "ns2_pairing_crypto.h"
#include "switch_gc_report_select.h"
#include "nso_gc_input.h"

volatile uint8_t g_gc_stage = 0;

volatile uint8_t g_gc_bad_report_id = 0;

// Native identity, with reference bcdDevice 1.11 to separate Windows driver caches.
static const uint8_t switch_gc_device_desc[] = {
    0x12,
    0x01,
    0x00, 0x02,
    0xEF,
    0x02,
    0x01,
    0x40,
    0x7E, 0x05,
    0x73, 0x20,
    0x11, 0x01,

    0x01,
    0x02,
    0x03,
    0x01,
};

static const char *switch_gc_strings[] = {
    (const char[]){0x09, 0x04},
    "Nintendo",
    "Nintendo GameCube Controller",
    "00",
    NULL,
    "If_Hid",
    NULL,
};

static const uint8_t switch_gc_report_desc[] = {
    0x05, 0x01,
    0x09, 0x05,
    0xA1, 0x01,
    0x85, 0x05,
    0x05, 0xFF,
    0x09, 0x01,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x95, 0x3F,
    0x75, 0x08,
    0x81, 0x02,
    0x85, 0x0A,
    0x09, 0x01,
    0x95, 0x02,
    0x81, 0x02,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x15,
    0x25, 0x01,
    0x95, 0x15,
    0x75, 0x01,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x81, 0x03,
    0x05, 0x01,
    0x09, 0x01,
    0xA1, 0x00,
    0x09, 0x30,
    0x09, 0x31,
    0x09, 0x33,
    0x09, 0x35,
    0x26, 0xFF, 0x0F,
    0x95, 0x04,
    0x75, 0x0C,
    0x81, 0x02,
    0xC0,
    0x05, 0xFF,
    0x09, 0x02,
    0x26, 0xFF, 0x00,
    0x95, 0x34,
    0x75, 0x08,
    0x81, 0x02,
    0x85, 0x03,
    0x09, 0x01,
    0x95, 0x3F,
    0x91, 0x02,
    0xC0,
};
_Static_assert(sizeof(switch_gc_report_desc) == 97,
               "GameCube HID report descriptor must be 97 bytes (Confirmed wDescriptorLength)");

#define SWITCH_GC_CONFIG_LEN 80
static const uint8_t switch_gc_config_desc[] = {
    0x09, 0x02, (SWITCH_GC_CONFIG_LEN & 0xFF), (SWITCH_GC_CONFIG_LEN >> 8),
    0x02,
    0x01,
    0x04,
    0xC0,

    0xFA,

    0x08, 0x0B, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x04, 0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x05,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x61, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x04,
    0x07, 0x05, 0x01, 0x03, 0x40, 0x00, 0x04,

    0x08, 0x0B, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x00,
    0x09, 0x04, 0x01, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x06,
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00,
};
_Static_assert(sizeof(switch_gc_config_desc) == SWITCH_GC_CONFIG_LEN,
               "GameCube configuration descriptor size must match wTotalLength");

const uint8_t *switch_gc_device_descriptor(void) { return switch_gc_device_desc; }
const uint8_t *switch_gc_config_descriptor(void) { return switch_gc_config_desc; }
const uint8_t *switch_gc_hid_report_descriptor(void) { return switch_gc_report_desc; }
const char **switch_gc_string_table(size_t *count) {
    *count = sizeof(switch_gc_strings) / sizeof(switch_gc_strings[0]);
    return switch_gc_strings;
}

#define GC_MS_OS_VENDOR_CODE 0x20

static const uint16_t switch_gc_ms_os_str[] = {
    0x0312, 'M', 'S', 'F', 'T', '1', '0', '0', GC_MS_OS_VENDOR_CODE};

const uint16_t *switch_gc_ms_os_string_descriptor(void) {
    return switch_gc_ms_os_str;
}

static const uint8_t switch_gc_ms_compat_id[] = {
    0x28, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x04, 0x00,
    0x01,
    0, 0, 0, 0, 0, 0, 0,
    0x01, 0x01,
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0,
};
_Static_assert(sizeof(switch_gc_ms_compat_id) == 40, "MS compat ID descriptor must be 40 bytes");

static const uint8_t switch_gc_ms_ext_props[] = {

    0x8E, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x05, 0x00,
    0x01, 0x00,

    0x84, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0x28, 0x00,
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0,
    'G', 0, 'U', 0, 'I', 0, 'D', 0, 0, 0,
    0x4E, 0x00, 0x00, 0x00,
    '{', 0, '6', 0, 'F', 0, '1', 0, '3', 0, '7', 0, '2', 0, '5', 0, 'E', 0,
    '-', 0, 'E', 0, 'F', 0, '0', 0, 'E', 0,
    '-', 0, '4', 0, 'F', 0, 'D', 0, '3', 0,
    '-', 0, 'A', 0, 'E', 0, '5', 0, 'F', 0,
    '-', 0, 'B', 0, '2', 0, 'D', 0, 'E', 0, '9', 0, '8', 0, '9', 0, 'E', 0,
    'C', 0, '8', 0, '2', 0, '5', 0, '}', 0, 0, 0,
};
_Static_assert(sizeof(switch_gc_ms_ext_props) == 142,
               "MS extended properties descriptor must be 142 bytes");

static uint8_t s_report_counter = 0;

void switch_gc_build_report(uint8_t *p) {
    nso_gc_input_t in;
    joypad_gc_get_input(0, &in);
    switch_gc_encode_report(&in, s_report_counter++, p);
}

static uint8_t s_selected_report_id = GC_REPORT_ID_NONE;
static uint32_t s_report05_counter = 0;

#define GC_RUMBLE_ON_AMPLITUDE 0xB0
#define GC_RUMBLE_PULSE_MS 40
static uint32_t s_rumble_pulse_until_ms = 0;
static bool s_rumble_pulse_armed = false;

static const uint8_t switch_gc_ctrl_identity[64] = {
    0x01, 0x00,
    'H', 'H', 'W', '5', '0', '0', '0', '9', '9', '9', '9', '9', '9', '9', 0x00, 0x00,
    0x7E, 0x05,
    0x73, 0x20,
    0x01, 0x04, 0x01,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
_Static_assert(sizeof(switch_gc_ctrl_identity) == 64, "GC EP0 identity block must be 64 bytes");

static const uint8_t SWITCH_GC_UNIT_ID[6] = {0x02, 0xBB, 0x5E, 0xAB, 0xA9, 0x3C};
static uint8_t switch_gc_ctrl_info[NS2_GC_EP0_INFO_LEN];

static const uint8_t GC_DEVICE_KEY_B1[16] = {
    0x5C, 0xF6, 0xEE, 0x79, 0x2C, 0xDF, 0x05, 0xE1,
    0xBA, 0x2B, 0x63, 0x25, 0xC4, 0x1A, 0x5F, 0x10};
static uint8_t s_gc_ltk[16];

// Synthetic reference calibration, not private data from a genuine unit.
static const uint8_t switch_gc_synthetic_cal_blk[40] = {
    0x01, 0xAD, 0xD9, 0x9A, 0x55, 0x56, 0x65, 0xA0, 0x00, 0x0A, 0xA0, 0x00, 0x0A, 0xE2,
    0x20, 0x0E, 0xE2, 0x20, 0x0E, 0x9A, 0xAD, 0xD9, 0x9A, 0xAD, 0xD9, 0x0A, 0xA5, 0x50,
    0x0A, 0xA5, 0x50, 0x2F, 0xF6, 0x62, 0x2F, 0xF6, 0x62, 0x0A, 0xFF, 0xFF};

static void switch_gc_mem_read(uint32_t addr, uint8_t len, uint8_t *out) {
    for (uint8_t i = 0; i < len; i++) {
        uint32_t a = addr + i;
        if (a >= 0x13000 && a < 0x13000 + sizeof(switch_gc_ctrl_identity)) {

            out[i] = switch_gc_ctrl_identity[a - 0x13000];
        } else if (a >= 0x13080 && a < 0x13080 + 40) {
            out[i] = switch_gc_synthetic_cal_blk[a - 0x13080];
        } else if (a >= 0x130C0 && a < 0x130C0 + 40) {
            out[i] = switch_gc_synthetic_cal_blk[a - 0x130C0];
        } else if (a >= 0x13100 && a < 0x13100 + 24) {
            out[i] = 0x00;

        } else if (a >= 0x13140 && a < 0x13140 + 9) {
            out[i] = 0x00;
        } else if (a == 0x1FA000) {
            out[i] = 0x00;

        } else {
            out[i] = 0xFF;
        }
    }
}

static uint32_t s_bulk_cmd_count = 0;

void switch_gc_vendor_dispatch(const uint8_t *c, uint32_t n) {
    if (n < 8 || c[1] != 0x91 || c[2] != 0x00) return;

    if (c[0] == 0x02 && n < 16) return;
    if (c[0] == 0x15 && (c[3] == 2 || c[3] == 4) && n < 25) return;
    uint8_t id = c[0], transport = c[2], sub = c[3];

    s_bulk_cmd_count++;
    if (s_selected_report_id == GC_REPORT_ID_NONE) {
        uint8_t capped = (uint8_t)(s_bulk_cmd_count > 9 ? 9 : s_bulk_cmd_count);
        uint8_t candidate = (uint8_t)(5 + capped);
        if (candidate > g_gc_stage) g_gc_stage = candidate;

        if (id == 0x03 && g_gc_stage < 20) g_gc_stage = 20;
    }

    static uint32_t last_unknown_log_ms = 0;

    uint8_t r[64];
    memset(r, 0, sizeof(r));
    r[0] = id;
    r[1] = 0x01;
    r[2] = transport;
    r[3] = sub;
    r[4] = 0x00;
    r[5] = 0xF8;
    uint8_t *d = &r[8];
    uint16_t dl = 0;

    switch (id) {
    case 0x03:
        if (sub == 0x0D || sub == 0x03) {

            d[0] = 0x01;
            dl = 4;
        } else if (sub == 0x0A) {

            if (g_gc_stage < 21) g_gc_stage = 21;
            s_selected_report_id = switch_gc_select_report(s_selected_report_id, c, n);
            if (s_selected_report_id == GC_REPORT_ID_NONE && n > 8) {
                g_gc_bad_report_id = c[8];
            }
            if (s_selected_report_id != GC_REPORT_ID_NONE) {

                g_gc_stage = 255;
            }
        }
        break;
    case 0x02: {
        uint32_t addr = (uint32_t)c[12] | ((uint32_t)c[13] << 8) |
                        ((uint32_t)c[14] << 16) | ((uint32_t)c[15] << 24);
        if (sub == 0x04) {
            uint8_t len = c[8];
            if (len > 0x28) len = 0x28;
            d[0] = len;
            d[4] = c[12]; d[5] = c[13]; d[6] = c[14]; d[7] = c[15];
            switch_gc_mem_read(addr, len, &d[8]);
            dl = (uint16_t)(8 + len);
        } else if (sub == 0x01) {

            d[0] = 0x28;
            d[4] = c[12]; d[5] = c[13]; d[6] = c[14]; d[7] = c[15];
            switch_gc_mem_read(addr, 0x28, &d[8]);
            dl = 8 + 0x28;
        } else if (sub == 0x05) {
            d[4] = c[12]; d[5] = c[13]; d[6] = c[14]; d[7] = c[15];
            dl = 8;
        }
        break;
    }
    case 0x0C:
        if (sub == 0x01) {
            uint8_t f = (n > 8) ? c[8] : 0;
            d[4] = (f & 0x01) ? 0x07 : 0x00;
            d[5] = (f & 0x02) ? 0x07 : 0x00;
            d[6] = (f & 0x04) ? 0x01 : 0x00;
            d[7] = (f & 0x80) ? 0x01 : 0x00;
            d[8] = (f & 0x10) ? 0x01 : 0x00;
            d[9] = (f & 0x20) ? 0x03 : 0x00;
            dl = 12;
        } else if (sub == 0x06) {
            d[4] = (n > 12) ? c[12] : 0;
            dl = 40;
        } else {
            dl = 4;
        }
        break;
    case 0x15:

        if (sub == 0x01) {

            memcpy(d, (const uint8_t[]){0x01, 0x04, 0x01,
                   switch_gc_ctrl_info[10], switch_gc_ctrl_info[11], switch_gc_ctrl_info[12],
                   switch_gc_ctrl_info[13], switch_gc_ctrl_info[14], switch_gc_ctrl_info[15]}, 9);
            dl = 9;
        } else if (sub == 0x02) {
            d[0] = 0x01;
            ns2_pairing_challenge(s_gc_ltk, &c[9], &d[1]);
            dl = 17;
        } else if (sub == 0x03) {
            d[0] = 0x01;
            dl = 1;
        } else if (sub == 0x04) {
            ns2_pairing_derive_ltk(&c[9], GC_DEVICE_KEY_B1, s_gc_ltk);
            d[0] = 0x01;
            memcpy(&d[1], GC_DEVICE_KEY_B1, 16);
            dl = 17;
        }
        break;
    case 0x16:
        dl = 24;
        break;
    case 0x07:
        d[0] = 0x00;
        dl = 1;
        break;
    case 0x09:
        if (n > 8) joypad_gc_set_player_leds(0, c[8]);
        dl = 0;
        break;

    case 0x10:
        ns2_gc_build_command_info(d);
        dl = NS2_GC_COMMAND_INFO_LEN;
        break;
    case 0x0B:

        if (sub == 0x03) { memcpy(d, (const uint8_t[]){0xA5, 0x0E, 0x00, 0x00}, 4); dl = 4; }
        else if (sub == 0x04) { memcpy(d, (const uint8_t[]){0x34, 0x00, 0x83, 0x00}, 4); dl = 4; }
        break;
    case 0x11:
        if (sub == 0x01) { d[0] = 0x03; dl = 4; }
        else if (sub == 0x03) {
            memcpy(d, (const uint8_t[]){
                0x01, 0xC0, 0x03, 0x00, 0x00, 0xE7, 0xD0, 0x1C, 0x3B, 0x79, 0x22, 0xA0, 0x3A,
                0x0A, 0xE8, 0x9C, 0x42, 0x58, 0xA0, 0x0B, 0x42, 0x0A, 0xE8, 0x9C, 0x41, 0x58,
                0xA0, 0x0B, 0x41}, 29);
            dl = 29;
        }
        break;
    case 0x01:

        r[1] = 0x04;
        dl = 0;
        break;
    case 0x18:
        if (sub == 0x01) { memcpy(d, (const uint8_t[]){0,0,0x40,0xF0,0,0,0x60,0}, 8); dl = 8; }
        else if (sub == 0x03) { d[0] = (n > 8) ? c[8] : 0; dl = 1; }
        break;
    default:

        dl = 0;
        break;
    }

    if (id != 0x03 && id != 0x02 && id != 0x0C && id != 0x15 && id != 0x16 &&
        id != 0x07 && id != 0x09 && id != 0x10 && id != 0x0B && id != 0x11 && id != 0x01 &&
        id != 0x18) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_unknown_log_ms > 1000) {
            printf("[GC] vendor bulk cmd id=0x%02x sub=0x%02x len=%lu answered with bare ACK\n",
                   id, sub, (unsigned long)n);
            last_unknown_log_ms = now_ms;
        }
    }

    uint16_t response_length = (uint16_t)(8 + dl);

    joypad_gc_queue_reply(r, response_length);
}

void switch_gc_init(void) {

    ns2_gc_build_ep0_info(SWITCH_GC_UNIT_ID, switch_gc_ctrl_info);
    s_report_counter = 0;
    s_report05_counter = 0;
    s_selected_report_id = GC_REPORT_ID_NONE;
    s_bulk_cmd_count = 0;
    memset(s_gc_ltk, 0, sizeof(s_gc_ltk));
    g_gc_bad_report_id = 0;
    g_gc_stage = 0;
    joypad_gc_set_rumble(0, 0, 0);
    s_rumble_pulse_armed = false;
}

void switch_gc_reset(void) {
    switch_gc_init();
}

void switch_gc_mount(void) {
    switch_gc_init();
    g_gc_stage = 3;
}

void switch_gc_build_report05(uint8_t *p) {
    nso_gc_input_t in;
    joypad_gc_get_input(0, &in);
    switch_gc_encode_report05(&in, s_report05_counter++, p);
}

void switch_gc_task(void) {
    joypad_gc_bulk_task();

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (s_rumble_pulse_armed && (int32_t)(now_ms - s_rumble_pulse_until_ms) >= 0) {
        joypad_gc_set_rumble(0, 0, 0);
        s_rumble_pulse_armed = false;
    }

    if (tud_hid_n_ready(0)) {
        uint8_t report[63];
        if (s_selected_report_id == 0x05) {
            switch_gc_build_report05(report);
            tud_hid_n_report(0, 0x05, report, sizeof(report));
        } else if (s_selected_report_id == 0x0A) {
            switch_gc_build_report(report);
            tud_hid_n_report(0, 0x0A, report, sizeof(report));
        }

    }
}

void switch_gc_hid_out_report(uint8_t report_id, const uint8_t *data, uint16_t len) {

    switch_gc_rumble_command_t command = switch_gc_decode_rumble(report_id, data, len);
    if (command == SWITCH_GC_RUMBLE_NO_CHANGE) return;

    if (command == SWITCH_GC_RUMBLE_STOP) {
        joypad_gc_set_rumble(0, 0, 0);
        s_rumble_pulse_armed = false;
        return;
    }

    if (command == SWITCH_GC_RUMBLE_OFF) {

        if (!s_rumble_pulse_armed) joypad_gc_set_rumble(0, 0, 0);
        return;
    }

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    joypad_gc_set_rumble(0, GC_RUMBLE_ON_AMPLITUDE, GC_RUMBLE_ON_AMPLITUDE);
    s_rumble_pulse_until_ms = now_ms + GC_RUMBLE_PULSE_MS;
    s_rumble_pulse_armed = true;
}

bool switch_gc_vendor_control_xfer(uint8_t rhport, uint8_t stage, const void *request_v) {
    tusb_control_request_t const *request = (tusb_control_request_t const *)request_v;
    if (stage != CONTROL_STAGE_SETUP) return true;
    // Validate transfer direction/type before responding from static IN buffers.
    if ((request->bmRequestType & 0x60) != 0x40) return false;
    if (request->bRequest == 4) {
        if ((request->bmRequestType & 0x80) || request->wLength) return false;
    } else if (!(request->bmRequestType & 0x80)) return false;

    if (request->bRequest == GC_MS_OS_VENDOR_CODE && request->wIndex == 0x0004) {
        return tud_control_xfer(rhport, request, (void *)switch_gc_ms_compat_id,
                                sizeof(switch_gc_ms_compat_id));
    }

    if (request->bRequest == GC_MS_OS_VENDOR_CODE && request->wIndex == 0x0005) {
        return tud_control_xfer(rhport, request, (void *)switch_gc_ms_ext_props,
                                sizeof(switch_gc_ms_ext_props));
    }

    if (request->bRequest == 0x02 || request->bRequest == 0x03 || request->bRequest == 0x04) {
        if (g_gc_stage < 4) g_gc_stage = 4;
    }
    switch (request->bRequest) {
        case 0x03: {
            uint16_t len = request->wLength < sizeof(switch_gc_ctrl_identity)
                               ? request->wLength : (uint16_t)sizeof(switch_gc_ctrl_identity);

            return tud_control_xfer(rhport, request, (void *)switch_gc_ctrl_identity, len);
        }
        case 0x02: {
            ns2_gc_build_ep0_info(SWITCH_GC_UNIT_ID, switch_gc_ctrl_info);
            uint16_t len = request->wLength < sizeof(switch_gc_ctrl_info)
                               ? request->wLength : (uint16_t)sizeof(switch_gc_ctrl_info);

            return tud_control_xfer(rhport, request, (void *)switch_gc_ctrl_info, len);
        }
        case 0x04:

            return tud_control_status(rhport, request);
    }

    return false;
}

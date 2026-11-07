// SPDX-License-Identifier: Apache-2.0
// Adapted from PicoSwitch2 e054bfd1311289f392069ff0d2447e01748c2124.
// See README.md and LICENSE.PicoSwitch2 in this directory.
#ifndef SWITCH_GC_H
#define SWITCH_GC_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Debugger milestones: 3=configured, 4=EP0 identity, 20=initialization,
// 21=report selection attempted, 255=supported input report selected.
extern volatile uint8_t g_gc_stage;
extern volatile uint8_t g_gc_bad_report_id;
const uint8_t *switch_gc_device_descriptor(void);
const uint8_t *switch_gc_config_descriptor(void);
const uint8_t *switch_gc_hid_report_descriptor(void);
const char **switch_gc_string_table(size_t *count);
const uint16_t *switch_gc_ms_os_string_descriptor(void);
void switch_gc_init(void);
void switch_gc_reset(void);
void switch_gc_mount(void);
void switch_gc_task(void);
void switch_gc_build_report(uint8_t *payload63);
void switch_gc_build_report05(uint8_t *payload63);
void switch_gc_hid_out_report(uint8_t id, const uint8_t *data, uint16_t len);
bool switch_gc_vendor_control_xfer(uint8_t rhport, uint8_t stage, const void *request);
#endif

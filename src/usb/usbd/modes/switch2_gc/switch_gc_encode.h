// SPDX-License-Identifier: Apache-2.0
// Adapted from PicoSwitch2 e054bfd1311289f392069ff0d2447e01748c2124.
// See README.md and LICENSE.PicoSwitch2 in this directory.
/*
 * Pure NSO GameCube report-0x0A encoder -- zero pico-sdk/TinyUSB dependency,
 * host-compilable and host-tested (see tests/firmware/test_switch_gc_report.c),
 * mirroring usb_mode_cycle.h/.c's own pattern for the same reason.
 */
#ifndef SWITCH_GC_ENCODE_H
#define SWITCH_GC_ENCODE_H

#include <stdint.h>

#include "nso_gc_input.h"  // nso_gc_input_t, NSOGC_MASK_*/NSOGC_EXTRA_*/NSOGC_NATIVE_*

// Construct the 63-byte report 0x0A input report body from an explicit input state and
// counter value. `out` must point to a buffer of at least 63 bytes and never includes the
// report ID (TinyUSB's tud_hid_n_report() prepends it separately). Layout Confirmed
// (docs/switch2-gc/protocol.md "Input Report 0x0A"); button-bitfield meaning Strong only.
// Native Z / independent L,R trigger detents / continuous analog L,R trigger are sourced
// from `in`'s gc_extra/left_trigger/right_trigger fields -- see firmware/src/console/pro/switch_pro.h's
// NSOGC_NATIVE_* comment and docs/switch2-gc/mapping.md "Internal normalized model requirements".
// Used by the real Switch 2 console; PC/Steam hosts use switch_gc_encode_report05() below.
void switch_gc_encode_report(const nso_gc_input_t *in, uint8_t counter, uint8_t out[63]);

// Construct the 63-byte report 0x05 input report body -- the common Switch-family format
// PC/Steam hosts actually select for this personality (Confirmed 2026-07-13, see the .c file's
// comment for the live-capture evidence). `counter` is a free-running 32-bit value (matches
// switch_pro2.c's own ns2_build_report_05() counter width, independent of report 0x0A's 8-bit
// one). Native Z uses the shared ZR slot. Independent trigger detents have no
// slot in this format; analog L/R is carried at payload offsets 0x3C/0x3D.
void switch_gc_encode_report05(const nso_gc_input_t *in, uint32_t counter, uint8_t out[63]);

#endif  // SWITCH_GC_ENCODE_H

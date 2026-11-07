// SPDX-License-Identifier: Apache-2.0
// Adapted from PicoSwitch2 e054bfd1311289f392069ff0d2447e01748c2124.
// See README.md and LICENSE.PicoSwitch2 in this directory.
#ifndef JOYPAD_GC_INPUT_H
#define JOYPAD_GC_INPUT_H
#include <stdint.h>
#include <stdbool.h>
#define NSOGC_MASK_Y (1U << 0)
#define NSOGC_MASK_X (1U << 1)
#define NSOGC_MASK_B (1U << 2)
#define NSOGC_MASK_A (1U << 3)
#define NSOGC_MASK_R (1U << 6)
#define NSOGC_MASK_ZR (1U << 7)
#define NSOGC_MASK_MINUS (1U << 0)
#define NSOGC_MASK_PLUS (1U << 1)
#define NSOGC_MASK_R3 (1U << 2)
#define NSOGC_MASK_L3 (1U << 3)
#define NSOGC_MASK_HOME (1U << 4)
#define NSOGC_MASK_CAPTURE (1U << 5)
#define NSOGC_MASK_DPAD_DOWN (1U << 0)
#define NSOGC_MASK_DPAD_UP (1U << 1)
#define NSOGC_MASK_DPAD_RIGHT (1U << 2)
#define NSOGC_MASK_DPAD_LEFT (1U << 3)
#define NSOGC_MASK_L (1U << 6)
#define NSOGC_MASK_ZL (1U << 7)
#define NSOGC_EXTRA_C (1U << 0)   // C (chat)
#define NSOGC_EXTRA_GL (1U << 1)  // GL (left grip)
#define NSOGC_EXTRA_GR (1U << 2)  // GR (right grip)
#define NSOGC_NATIVE_Z (1U << 0)          // native GameCube Z (distinct from Z/ZR routing elsewhere)
#define NSOGC_NATIVE_L_DETENT (1U << 1)   // independent left analog-trigger mechanical detent
#define NSOGC_NATIVE_R_DETENT (1U << 2)   // independent right analog-trigger mechanical detent
#define NSOGC_NATIVE_ZL (1U << 3)         // native GameCube ZL shoulder
#define SWITCH_STICK_MIN 0
#define SWITCH_STICK_MID 2048
#define SWITCH_STICK_MAX 4095
typedef struct {
    uint8_t buttons[3], extra, left_stick[3], right_stick[3];
    uint8_t battery_valid, battery_level, battery_charging;
    uint8_t left_trigger, right_trigger, gc_extra;
} nso_gc_input_t;
static inline void nso_gc_pack_stick(uint16_t x, uint16_t y, uint8_t out[3]) {
    out[0] = (uint8_t)(x & 0xFF);
    out[1] = (uint8_t)(((y & 0x0F) << 4) | ((x >> 8) & 0x0F));
    out[2] = (uint8_t)((y >> 4) & 0xFF);
}


#endif

// SPDX-License-Identifier: Apache-2.0
#ifndef JOYPAD_GC_TRANSPORT_H
#define JOYPAD_GC_TRANSPORT_H
#include <stdint.h>
#include "nso_gc_input.h"
void joypad_gc_get_input(uint8_t player, nso_gc_input_t *out);
void joypad_gc_set_rumble(uint8_t player, uint8_t left, uint8_t right);
void joypad_gc_set_player_leds(uint8_t player, uint8_t mask);
void joypad_gc_queue_reply(const uint8_t *data, uint16_t len);
void joypad_gc_bulk_task(void);
void switch_gc_vendor_dispatch(const uint8_t *data, uint32_t len);
const uint16_t *joypad_gc_string_descriptor(uint8_t index);
#endif

// SPDX-License-Identifier: Apache-2.0
// Adapted from PicoSwitch2 e054bfd1311289f392069ff0d2447e01748c2124.
// See README.md and LICENSE.PicoSwitch2 in this directory.
#include "switch_gc_report_select.h"

uint8_t switch_gc_select_report(uint8_t current, const uint8_t *c, uint32_t n) {
    if (n > 8 && (c[8] == 0x05 || c[8] == 0x0A)) return c[8];
    return current;
}

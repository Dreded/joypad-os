"""Exercise the firmware's actual origin/reply functions with a busy USB endpoint."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class GameCubeOriginProtocolTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cl"), "MSVC host compiler unavailable")
    def test_origin_reply_waits_retries_and_preserves_trigger_independence(self):
        source = (ROOT / "src/usb/usbd/modes/gc_adapter_mode.c").read_text(encoding="utf-8")
        descriptor = (ROOT / "src/usb/usbd/descriptors/gc_adapter_descriptors.h").read_text(encoding="utf-8")
        constants = "\n".join(re.findall(r"^#define GC_ADAPTER_\w+\s+0?x?[0-9A-Fa-f]+\b", descriptor, re.M))
        firmware = "\n".join(function(source, name) for name in (
            "static bool gc_adapter_mode_is_ready(",
            "static void gc_adapter_mode_build_origin(",
            "static void gc_adapter_mode_task(",
            "void gc_adapter_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)\n{",
            "static void gc_driver_reset(",
        ))
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
struct { struct { uint8_t status; } port[4]; } gc_adapter_report;
struct { uint8_t report_id, rumble[4]; } gc_adapter_rumble;
struct { uint8_t ep_in, epin_buf[37]; } _gc_itf;
static bool gc_adapter_origin_pending, gc_adapter_rumble_available;
static bool gc_adapter_init_pending, gc_adapter_initialized;
static uint8_t gc_adapter_init_ack;
static bool ready = true, busy, claim_ok = true, xfer_ok = true;
static int transfers;
static uint16_t sent_length;
static uint8_t sent[37];
static bool tud_ready(void) { return ready; }
static bool usbd_edpt_busy(uint8_t port, uint8_t ep) { (void)port; (void)ep; return busy; }
static bool usbd_edpt_claim(uint8_t port, uint8_t ep) { (void)port; (void)ep; return claim_ok; }
static bool usbd_edpt_xfer(uint8_t port, uint8_t ep, uint8_t* data, uint16_t len) {
    (void)port; (void)ep; transfers++;
    sent_length = len; memcpy(sent, data, len); return xfer_ok;
}
#define tu_memclr(ptr, size) memset(ptr, 0, size)
'''
        main = r'''
int main(void) {
    _gc_itf.ep_in = 0x82;
    gc_adapter_report.port[0].status = 0x14;
    gc_adapter_report.port[2].status = 0x14;
    uint8_t query[] = {0x12};
    gc_adapter_mode_handle_output(0, query, sizeof(query));
    assert(gc_adapter_origin_pending);
    memset(_gc_itf.epin_buf, 0xA5, sizeof(_gc_itf.epin_buf));
    busy = true;
    gc_adapter_mode_task();
    assert(transfers == 0 && gc_adapter_origin_pending);
    for (int i = 0; i < 37; i++) assert(_gc_itf.epin_buf[i] == 0xA5);
    busy = false; claim_ok = false;
    gc_adapter_mode_task();
    assert(transfers == 0 && gc_adapter_origin_pending);
    claim_ok = true; xfer_ok = false;
    gc_adapter_mode_task();
    assert(transfers == 1 && gc_adapter_origin_pending);
    xfer_ok = true;
    gc_adapter_mode_task();
    assert(transfers == 2 && !gc_adapter_origin_pending);
    assert(sent_length == 25 && sent[0] == 0x22);
    for (int port = 0; port < 4; port++) {
        uint8_t expected[] = {128, 127, 128, 127, 0, 0};
        if (port == 1 || port == 3) memset(expected, 0, sizeof(expected));
        assert(memcmp(sent + 1 + port * 6, expected, 6) == 0);
    }
    gc_adapter_mode_task();
    assert(transfers == 2); // no unsolicited/repeated origin reports
    uint8_t init[] = {0x13};
    gc_adapter_mode_handle_output(0, init, sizeof(init));
    assert(gc_adapter_init_pending);
    busy = true;
    gc_adapter_mode_task();
    assert(transfers == 2 && gc_adapter_init_pending);
    busy = false;
    gc_adapter_mode_task();
    assert(transfers == 3 && !gc_adapter_init_pending);
    assert(sent_length == 2 && sent[0] == 0x23 && sent[1] == 1);
    gc_adapter_mode_handle_output(0, init, sizeof(init));
    gc_adapter_mode_task();
    assert(transfers == 4 && sent_length == 2 && sent[0] == 0x23 && sent[1] == 0);
    gc_adapter_mode_handle_output(0x12, NULL, 0); // SET_REPORT's explicit ID
    assert(gc_adapter_origin_pending);
    gc_driver_reset(0);
    assert(!gc_adapter_origin_pending && !gc_adapter_init_pending);
    assert(!gc_adapter_initialized && _gc_itf.ep_in == 0);
    uint8_t rumble[] = {0x11, 1, 0, 2, 1};
    gc_adapter_mode_handle_output(0, rumble, sizeof(rumble));
    assert(gc_adapter_rumble_available);
    assert(gc_adapter_rumble.rumble[0] == 1 && gc_adapter_rumble.rumble[1] == 0);
    assert(gc_adapter_rumble.rumble[2] == 0 && gc_adapter_rumble.rumble[3] == 1);
    puts("PASS: origin reply, busy/failed-transfer retries, reset, rumble coexistence");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="gc-origin-") as directory:
            path = Path(directory)
            c_file = path / "origin_test.c"
            exe_file = path / "origin_test.exe"
            c_file.write_text(harness + constants + "\n" + firmware + main, encoding="utf-8")
            result = subprocess.run(
                [shutil.which("cl"), "/nologo", "/W4", "/TC", str(c_file),
                 f"/Fe{exe_file}", f"/Fo{path / 'origin_test.obj'}"],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe_file)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS:", result.stdout)


if __name__ == "__main__":
    unittest.main()

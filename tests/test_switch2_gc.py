"""Compile and execute native GameCube firmware on the host with mocked USB I/O."""
from pathlib import Path
import shutil
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "src/usb/usbd/modes/switch2_gc"
TESTS = ROOT / "tests/switch2_gc"
COMPILER = shutil.which("cl") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")


class NativeDescriptorTests(unittest.TestCase):
    def test_native_hid_wire_lengths(self):
        source = (PORT / "switch_gc.c").read_text()
        body = source.split("switch_gc_report_desc[] = {", 1)[1].split("};", 1)[0]
        body = re.sub(r"//[^\n]*", "", body)
        data = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)", body))
        self.assertEqual(len(data), 97)
        size = count = report_id = offset = 0
        bits = {}
        while offset < len(data):
            prefix = data[offset]
            length = (0, 1, 2, 4)[prefix & 3]
            offset += 1
            value = int.from_bytes(data[offset:offset + length], "little")
            offset += length
            tag = prefix & 0xFC
            if tag == 0x74: size = value
            elif tag == 0x94: count = value
            elif tag == 0x84: report_id = value
            elif tag in (0x80, 0x90, 0xB0):
                key = (tag, report_id)
                bits[key] = bits.get(key, 0) + size * count
        self.assertEqual(bits, {(0x80, 5): 504, (0x80, 10): 504, (0x90, 3): 504})


@unittest.skipUnless(COMPILER, "Host C compiler unavailable")
class NativeGameCubeTests(unittest.TestCase):
    def compile_run(self, sources, includes=(), cwd=None):
        with tempfile.TemporaryDirectory() as scratch:
            work = Path(cwd or scratch)
            exe = Path(scratch) / "test.exe"
            if Path(COMPILER).stem.lower() == "cl":
                command = [COMPILER, "/nologo", "/std:c11", "/W3", "/Fe:" + str(exe),
                           "/Fo:" + scratch + "/"]
                command += ["/I" + str(p) for p in [PORT, TESTS, *includes]]
            else:
                command = [COMPILER, "-std=c11", "-Wall", "-Wextra", "-o", str(exe)]
                command += ["-I" + str(p) for p in [PORT, TESTS, *includes]]
            command += [str(p) for p in sources]
            result = subprocess.run(command, cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_reference_golden_vectors(self):
        for test, source in [("switch_gc_report", "switch_gc_encode"),
                             ("switch_gc_report_select", "switch_gc_report_select"),
                             ("switch_gc_rumble_decode", "switch_gc_rumble_decode"),
                             ("ns2_gc_identity", "ns2_gc_identity"),
                             ("ns2_pairing_crypto", "ns2_pairing_crypto")]:
            with self.subTest(test=test):
                self.compile_run([TESTS / ("test_" + test + ".c"), PORT / (source + ".c")])

    def test_real_transport_and_protocol(self):
        with tempfile.TemporaryDirectory() as scratch:
            work = Path(scratch)
            (work / "pico").mkdir()
            (work / "pico/time.h").write_text('#include "usb_stub.h"\n')
            (work / "tusb.h").write_text('#include "usb_stub.h"\n')
            adapter = (ROOT / "src/usb/usbd/modes/switch2_gc_mode.c").read_text()
            adapter = adapter.replace('#include "../usbd_mode.h"', '#include "usb_stub.h"')
            adapter = adapter.replace('#include "../tusb_compat.h"', '')
            adapter = adapter.replace('"switch2_gc/', '"')
            (work / "transport.c").write_text(adapter + '\n#include "test_transport.c"\n')
            sources = [work / "transport.c"] + [PORT / (name + ".c") for name in
                ["switch_gc", "switch_gc_encode", "switch_gc_report_select",
                 "switch_gc_rumble_decode", "ns2_gc_identity", "ns2_pairing_crypto"]]
            self.compile_run(sources, [work, ROOT / "src"], work)


if __name__ == "__main__":
    unittest.main()

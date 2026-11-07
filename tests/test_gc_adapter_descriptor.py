"""Check the advertised HID lengths against GameCube adapter wire reports."""

from pathlib import Path
import re
import unittest


DESCRIPTOR = (
    Path(__file__).resolve().parents[1]
    / "src/usb/usbd/descriptors/gc_adapter_descriptors.h"
)


def report_items():
    source = DESCRIPTOR.read_text(encoding="utf-8")
    body = source.split("gc_adapter_report_descriptor[] = {", 1)[1].split("};", 1)[0]
    body = re.sub(r"//[^\n]*", "", body)
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", body))
    size = count = report_id = 0
    items = []
    offset = 0
    while offset < len(data):
        prefix = data[offset]
        offset += 1
        length = (0, 1, 2, 4)[prefix & 3]
        value = int.from_bytes(data[offset:offset + length], "little")
        offset += length
        item = prefix & 0xFC
        if item == 0x74:  # Report Size
            size = value
        elif item == 0x94:  # Report Count
            count = value
        elif item == 0x84:  # Report ID (not included in Report Count)
            report_id = value
        elif item in (0x80, 0x90, 0xB0):  # Input, Output, Feature
            items.append((item, report_id, size * count, value))
    return items


def report_lengths():
    bits = {}
    for item, report_id, bit_count, _ in report_items():
        key = (item, report_id)
        bits[key] = bits.get(key, 0) + bit_count
    return {key: (value + 7) // 8 + bool(key[1]) for key, value in bits.items()}


class GameCubeAdapterDescriptorTests(unittest.TestCase):
    def test_dolphin_five_byte_rumble_write_matches_hid_report(self):
        # Dolphin sends 0x11 followed by exactly four per-port rumble bytes.
        # Windows HID rejects a write shorter than the advertised report length.
        lengths = report_lengths()
        self.assertEqual(lengths[(0x90, 0x11)], 5)
        self.assertEqual(max(length for (item, _), length in lengths.items() if item == 0x90), 5)

    def test_four_port_input_report_stays_37_bytes(self):
        self.assertEqual(report_lengths()[(0x80, 0x21)], 37)

    def test_origin_query_and_four_port_response_are_advertised(self):
        lengths = report_lengths()
        self.assertIn((0x90, 0x12), lengths)
        self.assertEqual(lengths[(0x80, 0x22)], 25)
        self.assertEqual(max(length for (item, _), length in lengths.items() if item == 0x80), 37)

    def test_initialization_acknowledgement_is_two_bytes(self):
        self.assertEqual(report_lengths()[(0x80, 0x23)], 2)

    def test_libusb_can_detect_output_report_ids_from_value_fields(self):
        # libusb's Windows HID backend checks NumberOutputValueCaps and
        # HidP_GetValueCaps for report IDs, not button/array capabilities.
        # Otherwise it adds a zero ID prefix to Dolphin's already numbered
        # report, producing a six-byte write that Windows rejects.
        for item, report_id, _, flags in report_items():
            if item == 0x90:
                with self.subTest(report_id=report_id):
                    self.assertEqual(flags & 3, 2, "output must be Data,Variable")


if __name__ == "__main__":
    unittest.main()

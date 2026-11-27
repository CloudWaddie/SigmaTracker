"""Host-only interoperability regression tests; no connected hardware required."""
import unittest

from tools.serial_bridge import HidChord, hid_to_gemini, packets, plover_devices


def report(*indices):
    """Build a Plover HID report from zero-based, MSB-first lever indices."""
    mask = sum(1 << (63 - index) for index in set(indices))
    return b"\x50" + mask.to_bytes(8, "big")


def frame(*indices):
    """Build a reference Gemini PR frame from payload bit positions."""
    result = bytearray([0x80, 0, 0, 0, 0, 0])
    for index in indices:
        result[index // 7] |= 0x40 >> (index % 7)
    return bytes(result)


class BridgeTests(unittest.TestCase):
    def test_known_protocol_examples(self):
        # Independent examples from QMK's Gemini PR protocol documentation:
        # EUBG, WAZ and PHAPBGS.
        for indices, expected in [
            ((10, 11, 15, 17), "8000000c2800"),
            ((4, 7, 21), "800220000001"),
            ((3, 5, 7, 14, 15, 17, 19), "800520006a00"),
        ]:
            with self.subTest(indices=indices):
                state = int.from_bytes(report(*indices)[1:], "big")
                self.assertEqual(hid_to_gemini(state), bytes.fromhex(expected))

    def test_all_levers(self):
        reference = (
            7, 9, 10, 11, 12, 13, 14, 15,
            16, 17, 24, 25, 26, 27, 28, 29,
            30, 31, 32, 33, 34, 41, 1, 8,
            18, 22, 23, 2, 3, 4, 5, 6,
            35, 36, 37, 38, 39, 40, 0, 21, 19, 20,
        )
        for index in range(64):
            with self.subTest(index=index):
                expected = frame(reference[index]) if index < len(reference) else None
                self.assertEqual(hid_to_gemini(1 << (63 - index)), expected)

    def test_chord_aggregation(self):
        chord = HidChord()
        states = [report(0), report(0, 1), report(1, 2), report(2), report()]
        results = [chord.feed(state) for state in states]
        self.assertEqual(results, [None, None, None, None, frame(7, 9, 10)])

    def test_repeat_and_padding(self):
        chord = HidChord()
        for _ in range(2):
            self.assertIsNone(chord.feed(report(0) + b"\x00" * 10))
            self.assertEqual(chord.feed(report()), frame(7))
            self.assertIsNone(chord.feed(report()))

    def test_invalid_reports_do_not_complete_stroke(self):
        chord = HidChord()
        self.assertIsNone(chord.feed(report(0)))
        self.assertIsNone(chord.feed(b"\x50"))
        self.assertIsNone(chord.feed(b"\x01" + b"\x00" * 8))
        self.assertEqual(chord.feed(report()), frame(7))

    def test_extended_only_is_ignored(self):
        chord = HidChord()
        self.assertIsNone(chord.feed(report(63)))
        self.assertIsNone(chord.feed(report()))

    def test_gemini_resync(self):
        a, b = frame(7, 9), frame(24, 25)
        self.assertEqual(list(packets(b"\x00\x14\x80\x12" + a + b)), [a, b])

    def test_only_steno_hid_interface(self):
        class FakeHid:
            def enumerate(self):
                return [
                    {"usage_page": 1, "usage": 6, "path": b"keyboard"},
                    {"usage_page": 0x4C4A, "usage": 1, "path": b"console"},
                    {"usage_page": 0xFF50, "usage": 0x4C56, "path": b"steno"},
                    {"usage_page": 0xFF50, "usage": 0x4C56, "path": b"steno"},
                    {"usage_page": 0xFF50, "usage": 0x4C56},
                ]
        self.assertEqual([d["path"] for d in plover_devices(FakeHid())], [b"steno"])


if __name__ == "__main__":
    unittest.main()

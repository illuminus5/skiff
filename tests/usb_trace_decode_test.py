# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("trace", Path(__file__).resolve().parents[1] / "tools/fm1_usb_trace.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def pack(raw):
    p = [2]
    for off in range(0, len(raw), 7):
        group = raw[off:off + 7]
        p.append(sum((x >> 7) << i for i, x in enumerate(group)))
        p.extend(x & 127 for x in group)
    return p


class DecodeTest(unittest.TestCase):
    def test_request(self):
        raw = bytes.fromhex("a2 84 00 01 84 00 03 00")
        record = bytearray(294)
        record[:9] = raw + bytes([1])
        record[288:294] = bytes([1, 0, 0, 0, 1, 1])
        self.assertEqual(trace.decode(pack(record)), (1, [(True, raw)]))
        self.assertIn("GET_RES", trace.describe(raw))
        self.assertIn("wIndex=0x0084", trace.describe(raw))

    def test_empty(self):
        self.assertEqual(trace.decode(pack(bytes(294))), (0, []))

    def test_wrapped_order(self):
        raw = bytearray(294)
        for i in range(32):
            raw[9*i] = i
        raw[288:294] = bytes([35, 0, 0, 0, 3, 32])
        total, entries = trace.decode(pack(raw))
        self.assertEqual(total, 35)
        self.assertEqual([e[1][0] for e in entries], list(range(3, 32)) + [0, 1, 2])

    def test_malformed(self):
        bad_count = bytearray(294)
        bad_count[293] = 33
        bad_index = bytearray(294)
        bad_index[292] = 32
        bad_flag = bytearray(294)
        bad_flag[292:294] = bytes([1, 1])
        bad_flag[8] = 2
        for p in ([], [2], pack(bytes(294)) + [0], [1] + [0]*336,
                  pack(bad_count), pack(bad_index), pack(bad_flag), [2] + [128]*336):
            with self.assertRaises(ValueError):
                trace.decode(p)


if __name__ == "__main__":
    unittest.main()
